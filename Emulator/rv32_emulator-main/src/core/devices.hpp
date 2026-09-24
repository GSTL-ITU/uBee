// The devices that can be attached to the bus.
//
// Register layouts are in docs/memory-map.md. Offsets here are relative to
// whichever slot a device is attached to, so nothing hard-codes an address.
#pragma once

#include <string>
#include <vector>

#include "core/device.hpp"
#include "core/trap.hpp"

namespace rv::core {

/// A character sink.
///   +0x0  W  transmit one byte
///   +0x4  R  status; bit 0 = transmitter ready, always 1
class UartDevice final : public Device {
public:
    std::string_view type_name() const override { return "uart"; }
    std::string_view description() const override { return "character output"; }

    u32 read(u32 offset, u8 width) override;
    void write(u32 offset, u8 width, u32 value) override;
    void reset() override { output_.clear(); }

    u32 peek(u32 offset) const override { return offset == 0x4 ? 1u : 0u; }
    // The output is a growing string, which no peek/poke pair can express, so
    // its length rides in the aux slot instead.
    u64 aux_state() const override { return output_.size(); }
    void set_aux_state(u64 value) override { truncate(static_cast<std::size_t>(value)); }

    std::string state_summary() const override;

    const std::string& output() const { return output_; }
    void truncate(std::size_t length);

private:
    std::string output_;
};

/// A 32-bit output register standing in for board LEDs.
/// How many bits of a device are wired up. A board has the LEDs and switches
/// it has -- sixteen on a Basys 3, eight on a smaller one -- and a program that
/// writes bit 20 of an eight-LED bank is writing to nothing. Saying how wide it
/// is here is what lets the panel show that bank and not a row of thirty-two
/// lights that do not exist.
inline constexpr int kMaxWidth = 32;
constexpr u32 width_mask(int width) {
    return width >= kMaxWidth ? ~0u : (1u << width) - 1u;
}

class LedDevice final : public Device {
public:
    explicit LedDevice(int width = kMaxWidth);

    std::string_view type_name() const override { return "leds"; }
    std::string_view description() const override { return description_; }
    int width() const { return width_; }

    u32 read(u32 offset, u8 /*width*/) override { return peek(offset); }
    void write(u32 offset, u8 /*width*/, u32 value) override { poke(offset, value); }
    void reset() override { value_ = 0; }

    u32 peek(u32 offset) const override { return offset == 0 ? value_ : 0u; }
    void poke(u32 offset, u32 value) override {
        // Masked to the wired width. A bit with no light on the end of it does
        // not light up, and reading back what was written to one would be the
        // emulator agreeing with a program the board will not.
        if (offset == 0) value_ = value & width_mask(width_);
    }

    std::string state_summary() const override;

    u32 value() const { return value_; }
    void set_raw(u32 value) { value_ = value & width_mask(width_); }

private:
    int width_;
    std::string description_;
    u32 value_ = 0;
};

/// A row of switches the user flips.
class SwitchDevice final : public Device {
public:
    explicit SwitchDevice(int width = kMaxWidth);

    std::string_view type_name() const override { return "switches"; }
    std::string_view description() const override { return description_; }
    int width() const { return width_; }

    u32 read(u32 offset, u8 /*width*/) override { return peek(offset); }
    void write(u32 /*offset*/, u8 /*width*/, u32 /*value*/) override {}  // input only
    void reset() override {}  // physical switches do not move when a core resets

    u32 peek(u32 offset) const override { return offset == 0 ? value_ : 0u; }

    std::vector<InputControl> inputs() const override;
    void set_input(std::size_t index, u32 value) override;
    std::string state_summary() const override;

    u32 value() const { return value_; }
    void set_value(u32 value) { value_ = value & width_mask(width_); }

private:
    int width_;
    std::string description_;
    u32 value_ = 0;
};

/// A momentary button. Reads as 1 while held and can raise the external
/// interrupt line -- the simplest thing that behaves like asynchronous input.
///   +0x0  RW  a bit per button, 1 while held; writing 0 acknowledges
///
/// One by default, or a row of them: a board with four direction buttons is
/// one device with four bits, not four devices.
class ButtonDevice final : public Device {
public:
    explicit ButtonDevice(int count = 1);

    std::string_view type_name() const override { return "button"; }
    std::string_view description() const override { return description_; }
    int width() const { return count_; }

    u32 read(u32 offset, u8 /*width*/) override { return peek(offset); }
    void write(u32 offset, u8 /*width*/, u32 value) override { poke(offset, value); }
    void reset() override { pressed_ = 0; }

    u32 peek(u32 offset) const override { return offset == 0 ? pressed_ : 0u; }
    void poke(u32 offset, u32 value) override {
        if (offset == 0) pressed_ = value & width_mask(count_);
    }

    u32 interrupt_lines(u64 /*now*/) const override {
        return (pressed_ != 0 && raises_interrupt_) ? kIrqExternal : 0u;
    }

    std::vector<InputControl> inputs() const override;
    void set_input(std::size_t index, u32 value) override;
    std::string state_summary() const override;

    void set_raises_interrupt(bool raises) { raises_interrupt_ = raises; }
    bool raises_interrupt() const { return raises_interrupt_; }
    u32 pressed() const { return pressed_; }

private:
    int count_;
    std::string description_;
    u32 pressed_ = 0;
    bool raises_interrupt_ = true;
};

/// A number the user types in, standing in for a sensor or an ADC.
class ValueDevice final : public Device {
public:
    std::string_view type_name() const override { return "value"; }
    std::string_view description() const override { return "numeric input register"; }

    u32 read(u32 offset, u8 /*width*/) override { return peek(offset); }
    void write(u32 /*offset*/, u8 /*width*/, u32 /*value*/) override {}
    void reset() override {}

    u32 peek(u32 offset) const override { return offset == 0 ? value_ : 0u; }

    std::vector<InputControl> inputs() const override;
    void set_input(std::size_t index, u32 value) override;
    std::string state_summary() const override;

    u32 value() const { return value_; }
    void set_value(u32 value) { value_ = value; }

private:
    u32 value_ = 0;
};

/// The machine's clock, mirrored into the address space so a program can read
/// it with a plain `lw`. This is also `mtime`: the machine has no wall clock,
/// and tying time to a counter of retired instructions is what keeps interrupts
/// reproducible. Hardware `mtime` counts clock cycles, which is not the same
/// number -- see docs/memory-map.md.
///   +0x0  R  low word    +0x4  R  high word
class CycleDevice final : public Device {
public:
    std::string_view type_name() const override { return "mtime"; }
    std::string_view description() const override { return "instruction count / mtime"; }

    u32 read(u32 offset, u8 /*width*/) override { return peek(offset); }
    void write(u32 /*offset*/, u8 /*width*/, u32 /*value*/) override {}
    void reset() override { cycles_ = 0; }

    u32 peek(u32 offset) const override;
    std::string state_summary() const override;

    /// This device *is* the clock, so being told what it reads is all it does.
    void set_now(u64 now) override { cycles_ = now; }
    u64 cycles() const { return cycles_; }

private:
    u64 cycles_ = 0;
};

/// The timer compare register. A machine timer interrupt is pending while
/// `mtime >= mtimecmp`. Resets disarmed, so a program that never touches it
/// never sees one.
class TimerDevice final : public Device {
public:
    std::string_view type_name() const override { return "mtimecmp"; }
    std::string_view description() const override { return "timer compare"; }

    u32 read(u32 offset, u8 /*width*/) override { return peek(offset); }
    void write(u32 offset, u8 /*width*/, u32 value) override { poke(offset, value); }
    void reset() override { compare_ = kDisarmed; }

    u32 peek(u32 offset) const override { return offset == 0 ? compare_ : 0u; }
    void poke(u32 offset, u32 value) override {
        if (offset == 0) compare_ = value;
    }

    u32 interrupt_lines(u64 now) const override {
        return (armed() && now >= compare_) ? kIrqTimer : 0u;
    }
    std::string state_summary() const override;

    u32 compare() const { return compare_; }
    void set_compare(u32 value) { compare_ = value; }
    bool armed() const { return compare_ != kDisarmed; }

    static constexpr u32 kDisarmed = ~u32{0};

private:
    u32 compare_ = kDisarmed;
};

/// Software and external interrupt requests a program can raise itself.
///   +0x0  RW  software request, bit 0
///   +0x4  RW  external request, bit 0
class IrqDevice final : public Device {
public:
    std::string_view type_name() const override { return "irq"; }
    std::string_view description() const override { return "software/external IRQ requests"; }

    u32 read(u32 offset, u8 /*width*/) override { return peek(offset); }
    void write(u32 offset, u8 /*width*/, u32 value) override { poke(offset, value); }
    void reset() override { requests_ = 0; }

    u32 peek(u32 offset) const override;
    void poke(u32 offset, u32 value) override;

    u32 interrupt_lines(u64 /*now*/) const override { return requests_; }
    std::string state_summary() const override;

    u32 requests() const { return requests_; }
    void set_requests(u32 value) { requests_ = value & (kIrqSoftware | kIrqExternal); }

private:
    u32 requests_ = 0;
};

/// A block of memory in the device address space: extra RAM, or a ROM holding
/// a lookup table. Occupies as many slots as its size needs.
///
/// This is what makes "give me some ready-made memory" a peripheral rather than
/// a special case. It can be preloaded from a .mem file, marked read-only, and
/// edited by hand from the memory view like anything else -- and because writes
/// to it go through the ordinary peek/poke path, reverse stepping covers them
/// with no extra machinery.
class RamDevice final : public Device {
public:
    explicit RamDevice(u32 size_bytes, bool read_only = false);

    std::string_view type_name() const override { return read_only_ ? "rom" : "ram"; }
    std::string_view description() const override {
        return read_only_ ? "read-only memory block" : "memory block";
    }
    std::size_t slot_count() const override;

    u32 read(u32 offset, u8 width) override;
    void write(u32 offset, u8 width, u32 value) override;
    void reset() override;

    u32 peek(u32 offset) const override;
    void poke(u32 offset, u32 value) override;
    std::string state_summary() const override;

    u32 size() const { return static_cast<u32>(bytes_.size()); }
    bool read_only() const { return read_only_; }

    /// Fill from a word image and keep it as the reset contents, so resetting
    /// the machine restores a preloaded table rather than zeroing it.
    void load_words(const std::vector<Word>& words);

    /// The first words, offered as fields to type into. A block is as much an
    /// input as a bank of switches is -- an exercise that reads a table wants
    /// the table set by hand, and going through the memory view to do it means
    /// knowing the address the block landed at.
    std::vector<InputControl> inputs() const override;
    void set_input(std::size_t index, u32 value) override;

    /// How many words the panel offers. The rest of a large block is still
    /// reachable in the memory view; a hundred spin boxes would not be.
    static constexpr std::size_t kInputWords = 16;

private:
    std::vector<u8> bytes_;
    std::vector<u8> initial_;
    bool read_only_ = false;
};

/// A peripheral a student defines: a name, a few registers the program can read
/// and write, each one settable by hand, and a line it can raise an interrupt
/// on. Everything else in the catalogue is a fixed part someone else designed;
/// this is the one that can be whatever an exercise needs.
class CustomDevice final : public Device {
public:
    explicit CustomDevice(u32 word_count = 4, std::string name = "custom");

    std::string_view type_name() const override { return "custom"; }
    std::string_view description() const override { return description_; }
    std::size_t slot_count() const override;

    u32 read(u32 offset, u8 width) override;
    void write(u32 offset, u8 width, u32 value) override;
    void reset() override;

    u32 peek(u32 offset) const override;
    void poke(u32 offset, u32 value) override;

    u64 aux_state() const override { return raising_ ? 1u : 0u; }
    void set_aux_state(u64 value) override { raising_ = value != 0; }
    u32 interrupt_lines(u64 /*now*/) const override { return raising_ ? kIrqExternal : 0u; }

    std::vector<InputControl> inputs() const override;
    void set_input(std::size_t index, u32 value) override;
    std::string state_summary() const override;

    /// The same string as label(); kept because a custom peripheral is the one
    /// type for which a name is not optional -- it has nothing else to be
    /// called.
    const std::string& name() const { return label(); }
    u32 word_count() const { return static_cast<u32>(words_.size()); }

private:
    std::string description_;
    std::vector<u32> words_;
    bool raising_ = false;
};

/// What a function device computes. One entry so far, because one is what the
/// coursework needs: adding a row here breaks the switches in devices.cpp until
/// they handle it, which is where a new behaviour should have to be written
/// down rather than defaulted.
enum class FunctionOp : u8 {
    /// An unsigned product, twice the operand width -- Vivado's mult_gen as the
    /// IP homework configures it.
    Mul,
};

std::string_view function_op_name(FunctionOp op);
/// Resolve a name from a board file. False if nothing is called that.
bool parse_function_op(std::string_view name, FunctionOp& out);
/// Every behaviour that can be named, for listing in an error message.
const std::vector<std::string_view>& function_op_names();

/// The latency a function device has unless a file says otherwise: the seven
/// cycles the IP homework's controller counts. Any non-zero number would do --
/// what matters is that a program has to poll rather than read straight
/// through, which is the whole point of the exercise.
inline constexpr u32 kDefaultFunctionLatency = 7;
/// The operand width a function device has unless a file says otherwise: the
/// 8 x 8 the IP homework configures its multiplier as.
inline constexpr int kDefaultFunctionWidth = 8;
/// The widest operand a function device takes. The result is twice as wide and
/// is read as two halves of a register, so sixteen bits in is the most that
/// comes back out of a 32-bit word.
inline constexpr int kMaxFunctionWidth = 16;

/// A peripheral whose output is a *function* of its input, rather than whatever
/// was last written to it. This is what an IP core is, and it is the one shape
/// `custom` cannot take: a register file can only hand back what it was given.
///
///   +0x00  RW  operand A          +0x04  RW  operand B
///   +0x08  R   result, low half   +0x0c  R   result, high half
///   +0x10  R   status: 0 idle, 1 busy, 2 done
///
/// The protocol is the IP homework's, transcribed from its controller:
///
///   * **any** write restarts the operation, whichever register it names. That
///     is the rule its guideline is warning about -- a program that rewrites an
///     operand while waiting polls a counter that keeps resetting and hangs. It
///     is reproduced rather than smoothed over, because it is the part of
///     driving a peripheral the exercise is actually about.
///   * the result is *latched* when the operation completes, so while one is in
///     flight the result registers still read the previous one.
///   * the operands stay narrow and the result is read as two halves, because
///     that is the interface the homework specifies.
///
/// Nothing here counts for itself. The state is a function of the operands, the
/// cycle the last write landed on, and what the clock reads now -- which is why
/// reverse stepping puts it back with the machinery that was already there,
/// exactly as the timer's pending bit does.
class FunctionDevice final : public Device {
public:
    explicit FunctionDevice(FunctionOp op = FunctionOp::Mul,
                            int width = kDefaultFunctionWidth,
                            u32 latency = kDefaultFunctionLatency, std::string name = "function");

    /// Status values, as the homework numbers them.
    enum : u32 { kIdle = 0, kBusy = 1, kDone = 2 };

    static constexpr u32 kOffsetA = 0x00;
    static constexpr u32 kOffsetB = 0x04;
    static constexpr u32 kResultLow = 0x08;
    static constexpr u32 kResultHigh = 0x0c;
    static constexpr u32 kStatus = 0x10;

    std::string_view type_name() const override { return "function"; }
    std::string_view description() const override { return description_; }
    std::size_t slot_count() const override;

    u32 read(u32 offset, u8 /*width*/) override { return peek(offset); }
    void write(u32 offset, u8 width, u32 value) override;
    void reset() override;

    u32 peek(u32 offset) const override;
    void poke(u32 offset, u32 value) override;

    // Two things a write changes that no register holds: when the operation
    // started, and the result the one before it left behind.
    u64 aux_state() const override {
        return (static_cast<u64>(started_) << 32) | latched_;
    }
    void set_aux_state(u64 value) override {
        started_ = static_cast<u32>(value >> 32);
        latched_ = static_cast<u32>(value);
    }
    // Kept to 32 bits, like mtimecmp and for the same reason: a machine of a
    // few thousand instructions does not reach four billion cycles, and the
    // pair has to fit alongside the latched result in one aux field.
    void set_now(u64 now) override { now_ = static_cast<u32>(now); }

    std::vector<InputControl> inputs() const override;
    void set_input(std::size_t index, u32 value) override;
    std::string state_summary() const override;

    FunctionOp operation() const { return op_; }
    int width() const { return width_; }
    u32 latency() const { return latency_; }

    /// 0 idle, 1 busy, 2 done -- derived, never stored.
    u32 status() const;
    /// What the result registers currently hold: this operation's answer once
    /// it is done, the previous one's until then.
    u32 result() const;

private:
    /// The answer for the operands as they stand, whether or not it is ready.
    u32 compute() const;
    /// Take the result of a completed operation before a new write discards the
    /// operands that produced it. The only moment latched_ ever moves.
    void latch_if_done();

    FunctionOp op_;
    int width_;
    u32 latency_;
    std::string description_;

    u32 a_ = 0;
    u32 b_ = 0;
    /// The result of the last *completed* operation.
    u32 latched_ = 0;
    /// The cycle the current operation began, and what the clock reads now.
    /// Their difference is the whole of the timing.
    u32 started_ = 0;
    u32 now_ = 0;
};

/// A bank of 7-segment digits: one byte per digit, shown as glyphs rather than
/// as hex.
///   +0x0  RW  digits 0..3, one byte each (0x0-0xf, anything else blanks it)
class DisplayDevice final : public Device {
public:
    explicit DisplayDevice(int digits = 4);

    std::string_view type_name() const override { return "display"; }
    std::string_view description() const override { return "7-segment digits"; }

    u32 read(u32 offset, u8 /*width*/) override { return peek(offset); }
    void write(u32 offset, u8 /*width*/, u32 value) override { poke(offset, value); }
    void reset() override { value_ = kBlank; }

    u32 peek(u32 offset) const override { return offset == 0 ? value_ : 0u; }
    void poke(u32 offset, u32 value) override {
        if (offset == 0) value_ = value;
    }

    std::string state_summary() const override;
    /// The digits as text, for the interface to draw.
    std::string rendered() const;

    int digits() const { return digits_; }

private:
    static constexpr u32 kBlank = 0xffff'ffffu;
    int digits_ = 4;
    u32 value_ = kBlank;
};

}  // namespace rv::core
