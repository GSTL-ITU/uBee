// The data-side bus: DMEM plus a configurable set of devices.
//
// Instruction fetch does NOT go through here. The hart fetches from its own
// IMEM object, which is what makes this machine genuinely Harvard rather than
// von Neumann with two names for one memory.
//
// Devices are attached to slots at runtime, so a machine can be given the
// peripherals an exercise needs and dropped ones it does not.
#pragma once

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "core/device.hpp"
#include "core/devices.hpp"
#include "core/memory.hpp"
#include "isa/types.hpp"

namespace rv::core {

class Bus {
public:
    explicit Bus(u32 dmem_size);

    /// Resize data memory, keeping what still fits.
    void resize_dmem(u32 size) { dmem_.resize(size); }

    Memory& dmem() { return dmem_; }
    const Memory& dmem() const { return dmem_; }

    // ---- devices -----------------------------------------------------------

    /// Attach a device at `slot`. Overlapping an occupied slot is allowed:
    /// the later arrival wins the addresses they share, and the earlier one
    /// stays attached but shadowed there. Refusing would be the tidier rule,
    /// but a machine being built by hand spends most of its life in an
    /// inconsistent state, and stopping the work to complain is worse than
    /// showing what the overlap did. Returns nullptr only when the device
    /// would run off the end of the window.
    Device* attach(std::size_t slot, std::unique_ptr<Device> device);
    /// Remove whatever `slot` currently resolves to, revealing anything it was
    /// shadowing.
    bool detach(std::size_t slot);
    /// Remove one particular device, which is what a list of them needs when
    /// two share a slot.
    bool detach_device(const Device* device);
    void detach_all();

    /// Attached devices whose addresses overlap this range -- what a caller
    /// warns about before adding one.
    std::vector<Device*> devices_overlapping(std::size_t slot, std::size_t span) const;
    /// How many of a device's own slots still resolve to it. Zero means it is
    /// completely hidden; less than its span means partly.
    std::size_t reachable_slots(const Device* device) const;

    Device* device_at_slot(std::size_t slot) const;
    Device* device_for_address(Addr addr) const;
    /// Every attached device, each appearing once however many slots it spans.
    std::vector<Device*> devices() const;

    /// Find the first attached device of a given type, for the handful of
    /// places that need one specifically -- the hart updating mtime, or a test
    /// arming the timer. Returns nullptr when the machine has no such device,
    /// which is a legitimate configuration.
    template <typename T>
    T* find() const {
        for (Device* device : devices()) {
            if (auto* typed = dynamic_cast<T*>(device)) return typed;
        }
        return nullptr;
    }

    /// Attach the devices a machine has unless told otherwise.
    void attach_default_devices();

    // Cached pointers to the devices the rest of the machine asks for by name.
    // All nullable: a configuration with no timer, or no UART, is legitimate,
    // and callers have to say what happens then rather than assuming.
    UartDevice* uart() const { return uart_; }
    LedDevice* leds() const { return leds_; }
    SwitchDevice* switches() const { return switches_; }
    CycleDevice* cycles() const { return cycles_; }
    TimerDevice* timer() const { return timer_; }
    IrqDevice* irq() const { return irq_; }

    /// Whatever the UART has printed, or an empty string if there is no UART.
    const std::string& uart_output() const;

    // ---- access ------------------------------------------------------------

    static bool is_mmio(Addr addr) { return addr >= kMmioBase && addr - kMmioBase < kMmioSize; }

    MemResult load(Addr addr, u8 width, bool is_signed);
    MemResult store(Addr addr, u8 width, u32 value);

    /// Non-trapping, side-effect-free access, for the memory view, for editing
    /// by hand, and for the reverse-step delta. Devices participate through
    /// their own peek/poke, which is what lets a single one-word delta undo a
    /// write to a device with no per-device history.
    u32 peek_word(Addr addr) const;
    void poke_word(Addr addr, u32 value);

    /// The mip value the devices are asserting. Computed rather than stored,
    /// because the timer bit is a pure function of the cycle counter -- which
    /// is what makes interrupts reproducible under reverse stepping.
    u32 pending_interrupts(u64 now) const;

    /// Publish the clock to every device. Called whenever the cycle counter
    /// moves, undo included, so a device whose registers change with time never
    /// has to count for itself -- see Device::set_now.
    void set_now(u64 now);

    /// The device a step wrote to, or npos. The history needs this to know
    /// whose aux state to capture.
    static constexpr std::size_t kNoSlot = ~std::size_t{0};

    void reset();

private:
    void refresh_cache();
    /// Resolve `slots_` from `owned_`, later arrivals winning. `owned_` is the
    /// stacking order and this is the view of it, so attaching and detaching
    /// only have to keep the order right.
    void rebuild_slots();

    Memory dmem_;
    std::vector<std::unique_ptr<Device>> owned_;
    std::array<Device*, kMmioSlots> slots_{};

    UartDevice* uart_ = nullptr;
    LedDevice* leds_ = nullptr;
    SwitchDevice* switches_ = nullptr;
    CycleDevice* cycles_ = nullptr;
    TimerDevice* timer_ = nullptr;
    IrqDevice* irq_ = nullptr;
};

}  // namespace rv::core
