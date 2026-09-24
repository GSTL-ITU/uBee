#include "core/devices.hpp"

#include <algorithm>
#include <cstdarg>
#include <cstdio>

namespace rv::core {
namespace {

// printf-style, so clang can check the callers and stop warning that the
// format reaching vsnprintf is not a literal.
//
// gnu_printf on MinGW rather than printf: there the plain spelling means
// ms_printf, which predates C99 and rejects %zu. The runtime underneath is
// UCRT, which handles %zu correctly -- this tells the checker the same thing
// the toolchain choice already says.
#ifdef __MINGW32__
#define RV_PRINTF_LIKE(fmt, first) __attribute__((format(gnu_printf, fmt, first)))
#else
#define RV_PRINTF_LIKE(fmt, first) __attribute__((format(printf, fmt, first)))
#endif

RV_PRINTF_LIKE(1, 2) std::string summary(const char* format, ...) {
    char buffer[192];
    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer, sizeof buffer, format, args);
    va_end(args);
    return buffer;
}

std::string bit_string(u32 value, char one, char zero, int width = kMaxWidth) {
    std::string out;
    for (int index = width - 1; index >= 0; --index) {
        out.push_back(((value >> index) & 1u) != 0 ? one : zero);
    }
    return out;
}

/// "8-bit output register" -- the width belongs in what the device calls
/// itself, since it is the first thing worth knowing about it.
std::string width_description(int width, const char* what) {
    return std::to_string(width) + "-bit " + what;
}

}  // namespace

// ---------------------------------------------------------------------------
// UART
// ---------------------------------------------------------------------------

u32 UartDevice::read(u32 offset, u8 /*width*/) {
    return offset == 0x4 ? 1u : 0u;  // transmitter always ready
}

void UartDevice::write(u32 offset, u8 /*width*/, u32 value) {
    if (offset != 0x0) return;
    output_.push_back(static_cast<char>(value & 0xffu));
}

void UartDevice::truncate(std::size_t length) {
    if (length < output_.size()) output_.resize(length);
}

std::string UartDevice::state_summary() const {
    return summary("%zu bytes written", output_.size());
}

// ---------------------------------------------------------------------------
// LEDs
// ---------------------------------------------------------------------------

LedDevice::LedDevice(int width)
    : width_(std::clamp(width, 1, kMaxWidth)),
      description_(width_description(width_, "output register")) {}

std::string LedDevice::state_summary() const {
    return summary("%08x  ", value_) + bit_string(value_, '#', '.', width_);
}

// ---------------------------------------------------------------------------
// Switches
// ---------------------------------------------------------------------------

SwitchDevice::SwitchDevice(int width)
    : width_(std::clamp(width, 1, kMaxWidth)),
      description_(width_description(width_, "input register")) {}

std::vector<InputControl> SwitchDevice::inputs() const {
    return {InputControl{"switches", InputKind::Toggle, value_, width_, 0, width_mask(width_)}};
}

void SwitchDevice::set_input(std::size_t index, u32 value) {
    if (index == 0) value_ = value & width_mask(width_);
}

std::string SwitchDevice::state_summary() const {
    return summary("%08x  ", value_) + bit_string(value_, '1', '0', width_);
}

// ---------------------------------------------------------------------------
// Button
// ---------------------------------------------------------------------------

ButtonDevice::ButtonDevice(int count)
    : count_(std::clamp(count, 1, kMaxWidth)),
      description_(count_ == 1 ? "momentary input, can raise IRQ"
                               : std::to_string(count_) + " momentary inputs, can raise IRQ") {}

std::vector<InputControl> ButtonDevice::inputs() const {
    std::vector<InputControl> out;
    out.reserve(static_cast<std::size_t>(count_) + 1);
    for (int index = 0; index < count_; ++index) {
        out.push_back(InputControl{count_ == 1 ? "press" : "press " + std::to_string(index),
                                   InputKind::Button, (pressed_ >> index) & 1u, 1, 0, 1});
    }
    // Last, whatever the count, so the buttons read as a row and the line
    // reads as a switch on the side of it.
    out.push_back(
        InputControl{"raises IRQ", InputKind::Toggle, raises_interrupt_ ? 1u : 0u, 1, 0, 1});
    return out;
}

void ButtonDevice::set_input(std::size_t index, u32 value) {
    if (index < static_cast<std::size_t>(count_)) {
        const u32 bit = 1u << index;
        pressed_ = (value & 1u) != 0 ? (pressed_ | bit) : (pressed_ & ~bit);
        return;
    }
    if (index == static_cast<std::size_t>(count_)) raises_interrupt_ = (value & 1u) != 0;
}

std::string ButtonDevice::state_summary() const {
    std::string out = count_ == 1 ? (pressed_ != 0 ? "pressed" : "released")
                                  : bit_string(pressed_, 'v', '.', count_);
    if (raises_interrupt_) out += ", raises external IRQ";
    return out;
}

// ---------------------------------------------------------------------------
// Value
// ---------------------------------------------------------------------------

std::vector<InputControl> ValueDevice::inputs() const {
    return {InputControl{"value", InputKind::Number, value_, 32, 0, 0xffff'ffffu}};
}

void ValueDevice::set_input(std::size_t index, u32 value) {
    if (index == 0) value_ = value;
}

std::string ValueDevice::state_summary() const {
    return summary("%u  (0x%08x)", value_, value_);
}

// ---------------------------------------------------------------------------
// Cycle counter
// ---------------------------------------------------------------------------

u32 CycleDevice::peek(u32 offset) const {
    if (offset == 0x0) return static_cast<u32>(cycles_);
    if (offset == 0x4) return static_cast<u32>(cycles_ >> 32);
    return 0;
}

std::string CycleDevice::state_summary() const {
    return summary("%llu cycles", static_cast<unsigned long long>(cycles_));
}

// ---------------------------------------------------------------------------
// Timer
// ---------------------------------------------------------------------------

std::string TimerDevice::state_summary() const {
    return armed() ? summary("fires at cycle %u", compare_) : std::string("disarmed");
}

// ---------------------------------------------------------------------------
// Interrupt requests
// ---------------------------------------------------------------------------

u32 IrqDevice::peek(u32 offset) const {
    if (offset == 0x0) return (requests_ & kIrqSoftware) != 0 ? 1u : 0u;
    if (offset == 0x4) return (requests_ & kIrqExternal) != 0 ? 1u : 0u;
    return 0;
}

void IrqDevice::poke(u32 offset, u32 value) {
    // Only bit 0 of each register is meaningful; writing 0 clears the request,
    // which is how a handler acknowledges it.
    if (offset == 0x0) {
        requests_ = (requests_ & ~kIrqSoftware) | ((value & 1u) != 0 ? kIrqSoftware : 0u);
    } else if (offset == 0x4) {
        requests_ = (requests_ & ~kIrqExternal) | ((value & 1u) != 0 ? kIrqExternal : 0u);
    }
}

std::string IrqDevice::state_summary() const {
    return summary("software=%d external=%d", (requests_ & kIrqSoftware) != 0 ? 1 : 0,
                   (requests_ & kIrqExternal) != 0 ? 1 : 0);
}

// ---------------------------------------------------------------------------
// Memory block
// ---------------------------------------------------------------------------

RamDevice::RamDevice(u32 size_bytes, bool read_only)
    : bytes_((size_bytes + 3u) & ~3u, 0), read_only_(read_only) {
    initial_ = bytes_;
}

std::size_t RamDevice::slot_count() const {
    return std::max<std::size_t>(1, (bytes_.size() + kSlotSize - 1) / kSlotSize);
}

u32 RamDevice::read(u32 offset, u8 width) {
    if (offset + width > bytes_.size()) return 0;
    u32 value = 0;
    for (u8 index = 0; index < width; ++index) {
        value |= static_cast<u32>(bytes_[offset + index]) << (8 * index);  // little-endian
    }
    return value;
}

void RamDevice::write(u32 offset, u8 width, u32 value) {
    // A ROM ignores stores rather than trapping: that is what the hardware
    // does. Catching the mistake belongs in the assembler, not here.
    if (read_only_ || offset + width > bytes_.size()) return;
    for (u8 index = 0; index < width; ++index) {
        bytes_[offset + index] = static_cast<u8>(value >> (8 * index));
    }
}

u32 RamDevice::peek(u32 offset) const {
    const u32 aligned = offset & ~3u;
    if (aligned + 4 > bytes_.size()) return 0;
    return static_cast<u32>(bytes_[aligned]) | (static_cast<u32>(bytes_[aligned + 1]) << 8) |
           (static_cast<u32>(bytes_[aligned + 2]) << 16) |
           (static_cast<u32>(bytes_[aligned + 3]) << 24);
}

void RamDevice::poke(u32 offset, u32 value) {
    // Deliberately ignores read_only_: this is the debugger's path, and being
    // unable to edit a ROM from a memory view would be an obstacle rather than
    // a safeguard.
    const u32 aligned = offset & ~3u;
    if (aligned + 4 > bytes_.size()) return;
    for (u8 index = 0; index < 4; ++index) {
        bytes_[aligned + index] = static_cast<u8>(value >> (8 * index));
    }
}

void RamDevice::reset() { bytes_ = initial_; }

void RamDevice::load_words(const std::vector<Word>& words) {
    const std::size_t count = std::min(words.size(), bytes_.size() / 4);
    for (std::size_t index = 0; index < count; ++index) {
        poke(static_cast<u32>(index * 4), words[index]);
    }
    // Keep it as the reset image, so a preloaded table survives a reset.
    initial_ = bytes_;
}

std::string RamDevice::state_summary() const {
    return summary("%u bytes%s", static_cast<u32>(bytes_.size()),
                   read_only_ ? ", read-only" : "");
}

// ---------------------------------------------------------------------------
// FunctionDevice
// ---------------------------------------------------------------------------

namespace {

/// The names a board file may use. The switch in function_op_name is what makes
/// adding a behaviour break the build; this is what makes it appear in the
/// error message that lists them, and the two are kept side by side so that
/// adding a row to one is an obvious prompt to add it to the other.
struct FunctionOpEntry {
    std::string_view name;
    FunctionOp op;
};
constexpr FunctionOpEntry kFunctionOps[] = {
    {"mul", FunctionOp::Mul},
};

}  // namespace

std::string_view function_op_name(FunctionOp op) {
    switch (op) {
        case FunctionOp::Mul: return "mul";
    }
    return "";
}

bool parse_function_op(std::string_view name, FunctionOp& out) {
    for (const FunctionOpEntry& entry : kFunctionOps) {
        if (entry.name == name) {
            out = entry.op;
            return true;
        }
    }
    return false;
}

const std::vector<std::string_view>& function_op_names() {
    static const std::vector<std::string_view> names = [] {
        std::vector<std::string_view> out;
        for (const FunctionOpEntry& entry : kFunctionOps) out.push_back(entry.name);
        return out;
    }();
    return names;
}

FunctionDevice::FunctionDevice(FunctionOp op, int width, u32 latency, std::string name)
    : op_(op), width_(std::clamp(width, 1, kMaxFunctionWidth)), latency_(latency) {
    set_label(std::move(name));
    description_ = std::string(function_op_name(op_)) + ", " + std::to_string(width_) + " x " +
                   std::to_string(width_) + ", " + std::to_string(latency_) +
                   (latency_ == 1 ? " cycle" : " cycles");
}

std::size_t FunctionDevice::slot_count() const {
    constexpr u32 kBytes = kStatus + 4;
    return (kBytes + kSlotSize - 1) / kSlotSize;
}

u32 FunctionDevice::status() const {
    // A clock that has gone behind the start reads as a write that has just
    // landed, which is what it was: reverse stepping restores the cycle counter
    // and the start together, and this is the moment between them.
    const u32 elapsed = now_ >= started_ ? now_ - started_ : 0;
    if (elapsed == 0) return kIdle;
    return elapsed <= latency_ ? kBusy : kDone;
}

u32 FunctionDevice::compute() const {
    switch (op_) {
        case FunctionOp::Mul: return (a_ * b_) & width_mask(2 * width_);
    }
    return 0;
}

u32 FunctionDevice::result() const { return status() == kDone ? compute() : latched_; }

void FunctionDevice::latch_if_done() {
    if (status() == kDone) latched_ = compute();
}

u32 FunctionDevice::peek(u32 offset) const {
    switch (offset) {
        case kOffsetA: return a_;
        case kOffsetB: return b_;
        case kResultLow: return result() & width_mask(width_);
        case kResultHigh: return (result() >> width_) & width_mask(width_);
        case kStatus: return status();
        default: return 0;
    }
}

void FunctionDevice::write(u32 offset, u8 /*width*/, u32 value) {
    // Whatever it names, the write restarts the operation -- so the answer the
    // last one produced has to be taken now, before its operands are gone.
    latch_if_done();
    const u32 mask = width_mask(width_);
    if (offset == kOffsetA) {
        a_ = value & mask;
    } else if (offset == kOffsetB) {
        b_ = value & mask;
    }
    started_ = now_;
}

void FunctionDevice::poke(u32 offset, u32 value) {
    // The person's path, not the program's: setting a register by hand from the
    // memory view must not start the peripheral, or looking at a machine would
    // run it. The result and the status are computed, so there is nothing there
    // to set -- the same as poking a UART's ready bit.
    const u32 mask = width_mask(width_);
    if (offset == kOffsetA) {
        a_ = value & mask;
    } else if (offset == kOffsetB) {
        b_ = value & mask;
    }
}

void FunctionDevice::reset() {
    a_ = 0;
    b_ = 0;
    latched_ = 0;
    started_ = 0;
    now_ = 0;
}

std::vector<InputControl> FunctionDevice::inputs() const {
    const u32 mask = width_mask(width_);
    return {
        InputControl{"A", InputKind::Number, a_, width_, 0, mask},
        InputControl{"B", InputKind::Number, b_, width_, 0, mask},
    };
}

void FunctionDevice::set_input(std::size_t index, u32 value) {
    if (index == 0) poke(kOffsetA, value);
    if (index == 1) poke(kOffsetB, value);
}

std::string FunctionDevice::state_summary() const {
    static const char* const kStatusNames[] = {"idle", "busy", "done"};
    return summary("%s  a=%u b=%u -> %u  %s", std::string(function_op_name(op_)).c_str(), a_, b_,
                   result(), kStatusNames[status()]);
}

// ---------------------------------------------------------------------------
// Display
// ---------------------------------------------------------------------------

DisplayDevice::DisplayDevice(int digits) : digits_(std::clamp(digits, 1, 4)) {}

std::string DisplayDevice::rendered() const {
    std::string out;
    // Most significant digit first, which is how a number is read.
    for (int index = digits_ - 1; index >= 0; --index) {
        const u32 nibble = (value_ >> (8 * index)) & 0xffu;
        out.push_back(nibble <= 0xf ? "0123456789abcdef"[nibble] : '-');
    }
    return out;
}

std::string DisplayDevice::state_summary() const { return summary("[%s]", rendered().c_str()); }


std::vector<InputControl> RamDevice::inputs() const {
    const std::size_t words = std::min<std::size_t>(bytes_.size() / 4, kInputWords);
    std::vector<InputControl> out;
    out.reserve(words);
    for (std::size_t index = 0; index < words; ++index) {
        out.push_back(InputControl{"word[" + std::to_string(index) + "]", InputKind::Number,
                                   peek(static_cast<u32>(index * 4)), 32, 0, 0xffff'ffffu});
    }
    return out;
}

void RamDevice::set_input(std::size_t index, u32 value) {
    // Through poke, so read-only blocks are settable by hand for the same
    // reason the memory view can edit them: this is the person's path, not the
    // program's.
    poke(static_cast<u32>(index * 4), value);
    // A block set by hand is meant to stay set, so a reset restores what was
    // typed rather than what the file loaded.
    const std::size_t at = index * 4;
    if (at + 4 <= initial_.size()) {
        for (u8 byte = 0; byte < 4; ++byte) {
            initial_[at + byte] = static_cast<u8>(value >> (8 * byte));
        }
    }
}

// ---------------------------------------------------------------------------
// CustomDevice
// ---------------------------------------------------------------------------

CustomDevice::CustomDevice(u32 word_count, std::string name)
    : words_(std::max<u32>(1, word_count), 0) {
    set_label(std::move(name));
    description_ = std::to_string(words_.size()) +
                   (words_.size() == 1 ? " register" : " registers");
}

std::size_t CustomDevice::slot_count() const {
    const std::size_t bytes = words_.size() * 4;
    return std::max<std::size_t>(1, (bytes + kSlotSize - 1) / kSlotSize);
}

u32 CustomDevice::peek(u32 offset) const {
    const std::size_t index = offset / 4;
    return index < words_.size() ? words_[index] : 0u;
}

void CustomDevice::poke(u32 offset, u32 value) {
    const std::size_t index = offset / 4;
    if (index < words_.size()) words_[index] = value;
}

u32 CustomDevice::read(u32 offset, u8 /*width*/) { return peek(offset); }

void CustomDevice::write(u32 offset, u8 /*width*/, u32 value) { poke(offset, value); }

void CustomDevice::reset() {
    std::fill(words_.begin(), words_.end(), 0u);
    raising_ = false;
}

std::vector<InputControl> CustomDevice::inputs() const {
    std::vector<InputControl> out;
    out.reserve(words_.size() + 1);
    for (std::size_t index = 0; index < words_.size(); ++index) {
        out.push_back(InputControl{"reg[" + std::to_string(index) + "]", InputKind::Number,
                                   words_[index], 32, 0, 0xffff'ffffu});
    }
    // Last, so the registers read as a block and the line reads as a switch on
    // the side of it.
    out.push_back(InputControl{"raise IRQ", InputKind::Toggle, raising_ ? 1u : 0u, 1, 0, 1});
    return out;
}

void CustomDevice::set_input(std::size_t index, u32 value) {
    if (index < words_.size()) {
        words_[index] = value;
        return;
    }
    if (index == words_.size()) raising_ = (value & 1u) != 0;
}

std::string CustomDevice::state_summary() const {
    std::string out;
    for (std::size_t index = 0; index < words_.size() && index < 4; ++index) {
        if (index != 0) out += " ";
        out += summary("%08x", words_[index]);
    }
    if (words_.size() > 4) out += " …";
    if (raising_) out += "  IRQ";
    return out;
}

}  // namespace rv::core