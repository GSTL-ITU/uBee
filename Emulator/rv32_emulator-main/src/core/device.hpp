// The memory-mapped device interface.
//
// Devices are attached to slots at runtime rather than being fixed members of
// the bus, so a machine can be given the peripherals a particular exercise
// needs -- extra RAM, a button, a display -- and can drop the ones it does not.
//
// Two design points that carry most of the weight:
//
//   * peek/poke are side-effect-free. Reading a UART status register from a
//     memory viewer must not consume anything, and reverse stepping has to put
//     a written register back without re-triggering whatever the write did.
//     Because every device implements them, the debugger's existing one-word
//     memory delta already covers writes to devices -- no per-device history.
//
//   * inputs() describes the controls a person can operate, so the interface
//     can render switches and buttons for a device it has never heard of.
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "isa/types.hpp"

namespace rv::core {

inline constexpr Addr kMmioBase = 0xffff'0000u;
/// Bytes of address space per slot. Sixteen is enough for any register-shaped
/// device; a memory block simply takes several.
inline constexpr u32 kSlotSize = 16;
/// 64 slots, so the window is a kilobyte. Sixteen slots would have been enough
/// for registers, but a memory block is a peripheral here, and 256 bytes of
/// window leaves no room for a lookup table worth having.
inline constexpr std::size_t kMmioSlots = 64;
inline constexpr u32 kMmioSize = kMmioSlots * kSlotSize;

constexpr Addr slot_address(std::size_t slot) {
    return kMmioBase + static_cast<Addr>(slot) * kSlotSize;
}

/// A control the user interface can offer for a device.
enum class InputKind : u8 {
    /// A row of bits to flip, like the switches on a board.
    Toggle,
    /// A momentary press. Setting it to 1 asserts it for one step.
    Button,
    /// A number to type in, standing in for a sensor or an ADC.
    Number,
};

struct InputControl {
    std::string name;
    InputKind kind = InputKind::Number;
    u32 value = 0;
    /// Meaningful bits, for Toggle.
    int bit_count = 32;
    u32 minimum = 0;
    u32 maximum = 0xffff'ffffu;
};

class Device {
public:
    virtual ~Device() = default;

    /// The name used in a configuration file and in the "add device" list.
    virtual std::string_view type_name() const = 0;
    /// One line, shown next to the name.
    virtual std::string_view description() const = 0;

    /// How many slots the device occupies. Most take one; a RAM block takes as
    /// many as its size needs.
    virtual std::size_t slot_count() const { return 1; }

    // ---- the architectural interface ---------------------------------------

    virtual u32 read(u32 offset, u8 width) = 0;
    virtual void write(u32 offset, u8 width, u32 value) = 0;
    virtual void reset() = 0;

    // ---- the debugger's interface ------------------------------------------

    /// Read without side effects, for a memory viewer or a breakpoint check.
    virtual u32 peek(u32 /*offset*/) const { return 0; }
    /// Write without side effects, for undo and for editing memory by hand.
    virtual void poke(u32 /*offset*/, u32 /*value*/) {}

    /// State that peek/poke cannot express -- a UART's output length, a
    /// peripheral's start cycle and last result. Captured before every step
    /// that writes to this device, so reverse stepping can put it back. One
    /// field rather than a snapshot interface, because that is all any device
    /// has needed: everything else a write changes is a register, and registers
    /// are covered by the ordinary one-word memory delta.
    virtual u64 aux_state() const { return 0; }
    virtual void set_aux_state(u64 /*value*/) {}

    // ---- time ----------------------------------------------------------------

    /// What the machine's clock reads, pushed whenever it moves -- including
    /// backwards, when a step is undone. A device whose registers change with
    /// time reads this rather than counting for itself, so that its state stays
    /// a function of the cycle counter and reverse stepping restores it for
    /// free. See docs/memory-map.md for what the clock counts.
    virtual void set_now(u64 /*now*/) {}

    // ---- interrupts ---------------------------------------------------------

    /// The mip bits this device is asserting. `now` is the cycle counter, which
    /// is this machine's clock -- see docs/memory-map.md for why. Passed rather
    /// than read from set_now, because nothing may be stored for it: a stored
    /// pending bit is one that survives an undo.
    virtual u32 interrupt_lines(u64 /*now*/) const { return 0; }

    // ---- the user interface -------------------------------------------------

    /// Controls a person can operate. Empty for output-only devices.
    virtual std::vector<InputControl> inputs() const { return {}; }
    /// Apply a control's value. `index` is into the vector inputs() returned.
    virtual void set_input(std::size_t /*index*/, u32 /*value*/) {}

    /// A short human-readable summary of what the device currently holds,
    /// shown in the device list.
    virtual std::string state_summary() const { return {}; }

    /// Where the device was attached. Set by the bus.
    std::size_t slot() const { return slot_; }
    void set_slot(std::size_t slot) { slot_ = slot; }
    Addr base_address() const { return slot_address(slot_); }

    /// What this one is called. A machine may hold several of a kind -- two
    /// UARTs, one to a terminal and one to a sensor -- and once it does, the
    /// type name stops identifying anything. Empty means "no name given", and
    /// callers fall back to the type; storing the fallback instead would make
    /// every device look deliberately named.
    const std::string& label() const { return label_; }
    void set_label(std::string label) { label_ = std::move(label); }
    /// What to call it on screen: its name if it has one, its type if not.
    std::string display_name() const {
        return label_.empty() ? std::string(type_name()) : label_;
    }

private:
    std::size_t slot_ = 0;
    std::string label_;
};

}  // namespace rv::core
