// Trap causes and halt reasons.
#pragma once

#include "isa/types.hpp"

namespace rv::core {

/// Exception codes as written to mcause. Values are fixed by the privileged
/// spec; do not renumber.
enum class TrapCause : u32 {
    None = 0xffff'ffff,
    InstructionAddressMisaligned = 0,
    InstructionAccessFault = 1,
    IllegalInstruction = 2,
    Breakpoint = 3,
    LoadAddressMisaligned = 4,
    LoadAccessFault = 5,
    StoreAddressMisaligned = 6,
    StoreAccessFault = 7,
    EnvironmentCallFromMMode = 11,
};

/// Interrupt codes as written to the low bits of mcause, with bit 31 set.
/// Values are fixed by the privileged spec; do not renumber.
enum class InterruptCause : u32 {
    MachineSoftware = 3,
    MachineTimer = 7,
    MachineExternal = 11,
};

/// Bit positions shared by mie and mip.
inline constexpr u32 kIrqSoftware = 1u << 3;
inline constexpr u32 kIrqTimer = 1u << 7;
inline constexpr u32 kIrqExternal = 1u << 11;
inline constexpr u32 kIrqAll = kIrqSoftware | kIrqTimer | kIrqExternal;

/// The bit in mcause that says "this was an interrupt, not an exception".
inline constexpr u32 kMcauseInterrupt = 0x8000'0000u;

const char* interrupt_cause_name(InterruptCause cause);

/// Why the machine stopped. Distinct from a trap: a trap is handled by the
/// program (control transfers to mtvec), a halt returns control to the user.
enum class HaltReason : u8 {
    None,
    Ebreak,        // ebreak with EbreakBehavior::HaltToDebugger -- normal program end
    Ecall,         // ecall with no handler installed
    Watchdog,      // instruction budget exhausted; almost always an infinite loop
    UnhandledTrap, // a trap fired but mtvec is 0, so there is nowhere to go
    /// wfi with no interrupt armed and none pending. Nothing can ever wake the
    /// hart, so waiting would be an infinite loop with extra steps.
    WaitingForever,
};

/// What `ebreak` does. The self-checking .s test programs and the natural
/// "end of program" idiom both need HaltToDebugger, but the trap path must
/// exist and be testable for the Zicsr lessons.
enum class EbreakBehavior : u8 { HaltToDebugger, TrapToMtvec };

const char* trap_cause_name(TrapCause cause);
const char* halt_reason_name(HaltReason reason);

}  // namespace rv::core
