#include "core/bus.hpp"

#include <algorithm>

namespace rv::core {
namespace {

std::size_t slot_of(Addr addr) { return ((addr - kMmioBase) / kSlotSize) % kMmioSlots; }

/// Offset within the device occupying `addr`, which for a multi-slot device
/// spans further than one slot.
u32 offset_of(Addr addr, const Device& device) {
    return addr - slot_address(device.slot());
}

}  // namespace

Bus::Bus(u32 dmem_size) : dmem_(0, dmem_size) { attach_default_devices(); }

// ---------------------------------------------------------------------------
// Devices
// ---------------------------------------------------------------------------

Device* Bus::attach(std::size_t slot, std::unique_ptr<Device> device) {
    if (device == nullptr) return nullptr;
    const std::size_t span = std::max<std::size_t>(1, device->slot_count());
    if (slot + span > kMmioSlots) return nullptr;

    device->set_slot(slot);
    Device* raw = device.get();
    owned_.push_back(std::move(device));
    rebuild_slots();
    refresh_cache();
    return raw;
}

void Bus::rebuild_slots() {
    slots_.fill(nullptr);
    for (const std::unique_ptr<Device>& device : owned_) {
        const std::size_t base = device->slot();
        const std::size_t span = std::max<std::size_t>(1, device->slot_count());
        for (std::size_t index = base; index < base + span && index < kMmioSlots; ++index) {
            slots_[index] = device.get();
        }
    }
}

std::vector<Device*> Bus::devices_overlapping(std::size_t slot, std::size_t span) const {
    std::vector<Device*> out;
    for (const std::unique_ptr<Device>& device : owned_) {
        const std::size_t base = device->slot();
        const std::size_t other = std::max<std::size_t>(1, device->slot_count());
        if (base < slot + span && slot < base + other) out.push_back(device.get());
    }
    return out;
}

std::size_t Bus::reachable_slots(const Device* device) const {
    if (device == nullptr) return 0;
    const std::size_t base = device->slot();
    const std::size_t span = std::max<std::size_t>(1, device->slot_count());
    std::size_t count = 0;
    for (std::size_t index = base; index < base + span && index < kMmioSlots; ++index) {
        if (slots_[index] == device) ++count;
    }
    return count;
}

void Bus::refresh_cache() {
    uart_ = find<UartDevice>();
    leds_ = find<LedDevice>();
    switches_ = find<SwitchDevice>();
    cycles_ = find<CycleDevice>();
    timer_ = find<TimerDevice>();
    irq_ = find<IrqDevice>();
}

const std::string& Bus::uart_output() const {
    static const std::string kNone;
    return uart_ != nullptr ? uart_->output() : kNone;
}

bool Bus::detach(std::size_t slot) {
    if (slot >= kMmioSlots || slots_[slot] == nullptr) return false;
    return detach_device(slots_[slot]);
}

bool Bus::detach_device(const Device* device) {
    const auto gone = std::remove_if(owned_.begin(), owned_.end(),
                                     [device](const std::unique_ptr<Device>& held) {
                                         return held.get() == device;
                                     });
    if (gone == owned_.end()) return false;
    owned_.erase(gone, owned_.end());
    rebuild_slots();
    refresh_cache();
    return true;
}

void Bus::detach_all() {
    owned_.clear();
    slots_.fill(nullptr);
    refresh_cache();
}

Device* Bus::device_at_slot(std::size_t slot) const {
    return slot < kMmioSlots ? slots_[slot] : nullptr;
}

Device* Bus::device_for_address(Addr addr) const {
    if (!is_mmio(addr)) return nullptr;
    return slots_[slot_of(addr)];
}

std::vector<Device*> Bus::devices() const {
    std::vector<Device*> out;
    out.reserve(owned_.size());
    for (const std::unique_ptr<Device>& device : owned_) out.push_back(device.get());
    return out;
}

void Bus::attach_default_devices() {
    // The layout documented in docs/memory-map.md. A configuration file
    // replaces this wholesale rather than adding to it, so a machine described
    // in a file gets exactly what the file says.
    attach(0, std::make_unique<UartDevice>());
    attach(1, std::make_unique<LedDevice>());
    attach(2, std::make_unique<SwitchDevice>());
    attach(3, std::make_unique<CycleDevice>());
    attach(4, std::make_unique<TimerDevice>());
    attach(5, std::make_unique<IrqDevice>());
}

// ---------------------------------------------------------------------------
// Access
// ---------------------------------------------------------------------------

MemResult Bus::load(Addr addr, u8 width, bool is_signed) {
    if (!is_mmio(addr)) return dmem_.read(addr, width, is_signed);

    if ((addr & (width - 1u)) != 0) return MemResult::failure(TrapCause::LoadAddressMisaligned);
    Device* device = device_for_address(addr);
    // An address in the window with nothing attached faults rather than
    // reading as zero: a program touching a peripheral that is not there
    // should be told, not quietly given nothing.
    if (device == nullptr) return MemResult::failure(TrapCause::LoadAccessFault);
    return MemResult::success(device->read(offset_of(addr, *device), width));
}

MemResult Bus::store(Addr addr, u8 width, u32 value) {
    if (!is_mmio(addr)) return dmem_.write(addr, width, value);

    if ((addr & (width - 1u)) != 0) return MemResult::failure(TrapCause::StoreAddressMisaligned);
    Device* device = device_for_address(addr);
    if (device == nullptr) return MemResult::failure(TrapCause::StoreAccessFault);
    device->write(offset_of(addr, *device), width, value);
    return MemResult::success(0);
}

u32 Bus::peek_word(Addr addr) const {
    if (!is_mmio(addr)) return dmem_.read_word_raw(addr);
    // Devices participate, through their own side-effect-free path. This is
    // what lets the memory view show a peripheral's registers, and what lets a
    // single one-word delta undo a write to one.
    Device* device = device_for_address(addr);
    return device == nullptr ? 0u : device->peek(offset_of(addr, *device) & ~3u);
}

void Bus::poke_word(Addr addr, u32 value) {
    if (!is_mmio(addr)) {
        dmem_.write_word_raw(addr, value);
        return;
    }
    if (Device* device = device_for_address(addr)) {
        device->poke(offset_of(addr, *device) & ~3u, value);
    }
}

u32 Bus::pending_interrupts(u64 now) const {
    u32 pending = 0;
    for (const std::unique_ptr<Device>& device : owned_) {
        pending |= device->interrupt_lines(now);
    }
    return pending;
}

void Bus::set_now(u64 now) {
    for (const std::unique_ptr<Device>& device : owned_) device->set_now(now);
}

void Bus::reset() {
    dmem_.clear();
    for (const std::unique_ptr<Device>& device : owned_) device->reset();
}

}  // namespace rv::core
