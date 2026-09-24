#include "core/bus.hpp"

#include <algorithm>

namespace rv::core {
namespace {

u32 offset_of(Addr addr, const Device& device) { return addr - device.base_address(); }

}  // namespace

Bus::Bus(u32 dmem_size) : dmem_(0, dmem_size) { attach_default_devices(); }

// ---------------------------------------------------------------------------
// Devices
// ---------------------------------------------------------------------------

Device* Bus::attach(std::size_t slot, std::unique_ptr<Device> device) {
    if (device == nullptr) return nullptr;
    const std::size_t span = std::max<std::size_t>(1, device->slot_count());
    if (slot + span > kMmioSlots) return nullptr;
    return attach_at(slot_address(slot), std::move(device));
}

Device* Bus::attach_at(Addr base, std::unique_ptr<Device> device) {
    if (device == nullptr) return nullptr;

    device->set_base_address(base);
    if (base >= kMmioBase && base - kMmioBase < kMmioSize &&
        ((base - kMmioBase) % kSlotSize) == 0) {
        const std::size_t slot = (base - kMmioBase) / kSlotSize;
        const std::size_t span = std::max<std::size_t>(1, device->slot_count());
        if (slot + span <= kMmioSlots) {
            device->set_slot(slot);
        } else {
            // Absolute placement outside the slot table: keep a sentinel slot
            // so slot-based UI helpers do not claim a false window index.
            device->set_slot(kMmioSlots);
        }
    } else {
        device->set_slot(kMmioSlots);
    }

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
        if (base >= kMmioSlots) continue;
        const std::size_t span = std::max<std::size_t>(1, device->slot_count());
        for (std::size_t index = base; index < base + span && index < kMmioSlots; ++index) {
            slots_[index] = device.get();
        }
    }
}

std::vector<Device*> Bus::devices_overlapping(std::size_t slot, std::size_t span) const {
    return devices_overlapping_address(slot_address(slot),
                                       static_cast<u32>(std::max<std::size_t>(1, span) * kSlotSize));
}

std::vector<Device*> Bus::devices_overlapping_address(Addr base, u32 span_bytes) const {
    std::vector<Device*> out;
    const Addr end = base + span_bytes;
    for (const std::unique_ptr<Device>& device : owned_) {
        const Addr other = device->base_address();
        const Addr other_end = other + device->span_bytes();
        if (other < end && base < other_end) out.push_back(device.get());
    }
    return out;
}

std::size_t Bus::reachable_slots(const Device* device) const {
    if (device == nullptr) return 0;
    const std::size_t base = device->slot();
    if (base >= kMmioSlots) {
        // Outside the legacy window: reachable if still the owner of its base.
        return device_for_address(device->base_address()) == device ? 1 : 0;
    }
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
    // Later arrivals win overlapping addresses -- walk owned_ reverse.
    for (auto it = owned_.rbegin(); it != owned_.rend(); ++it) {
        if ((*it)->contains_address(addr)) return it->get();
    }
    return nullptr;
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
    if (Device* device = device_for_address(addr)) {
        if ((addr & (width - 1u)) != 0) return MemResult::failure(TrapCause::LoadAddressMisaligned);
        return MemResult::success(device->read(offset_of(addr, *device), width));
    }
    if (dmem_.contains(addr)) return dmem_.read(addr, width, is_signed);
    // Empty region of the legacy MMIO window still faults rather than reading
    // as zero: a program touching a peripheral that is not there should know.
    if (is_mmio(addr)) return MemResult::failure(TrapCause::LoadAccessFault);
    return MemResult::failure(TrapCause::LoadAccessFault);
}

MemResult Bus::store(Addr addr, u8 width, u32 value) {
    if (Device* device = device_for_address(addr)) {
        if ((addr & (width - 1u)) != 0) {
            return MemResult::failure(TrapCause::StoreAddressMisaligned);
        }
        device->write(offset_of(addr, *device), width, value);
        return MemResult::success(0);
    }
    if (dmem_.contains(addr)) return dmem_.write(addr, width, value);
    if (is_mmio(addr)) return MemResult::failure(TrapCause::StoreAccessFault);
    return MemResult::failure(TrapCause::StoreAccessFault);
}

u32 Bus::peek_word(Addr addr) const {
    if (Device* device = device_for_address(addr)) {
        return device->peek(offset_of(addr, *device) & ~3u);
    }
    return dmem_.read_word_raw(addr);
}

void Bus::poke_word(Addr addr, u32 value) {
    if (Device* device = device_for_address(addr)) {
        device->poke(offset_of(addr, *device) & ~3u, value);
        return;
    }
    dmem_.write_word_raw(addr, value);
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
