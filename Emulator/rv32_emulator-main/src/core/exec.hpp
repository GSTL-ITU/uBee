// Instruction execution.
//
// execute() is a pure state transition: it mutates the register file, the CSRs
// and memory, and reports where the pc should go next and whether anything
// trapped. Committing the pc and entering the trap handler is the hart's job,
// which keeps this function free of control-flow policy.
#pragma once

#include "core/bus.hpp"
#include "core/cpustate.hpp"
#include "core/trap.hpp"
#include "isa/decode.hpp"

namespace rv::core {

struct ExecResult {
    Addr next_pc = 0;

    bool trapped = false;
    TrapCause cause = TrapCause::None;
    u32 tval = 0;

    HaltReason halt = HaltReason::None;

    /// Cycles to skip past, for wfi. Time here is the cycle counter, so
    /// "waiting" means advancing it to the next event rather than spinning.
    u64 advance_cycles = 0;

    // What this instruction touched, and what was there before.
    //
    // RV32 has a property that makes this cheap: an instruction writes at most
    // one register, at most one CSR and at most one memory location. So a
    // fixed-size record is a complete inverse, and the executor -- which
    // already knows what it wrote -- fills it in for free. Nothing has to be
    // recovered afterwards by diffing 32 registers and 16 KB of memory.
    RegIdx reg_written = kNoReg;
    u32 reg_old = 0;

    CsrAddr csr_written = 0;
    bool wrote_csr = false;
    u32 csr_old = 0;

    Addr mem_written = kNoAddr;
    u8 mem_width = 0;
    /// The target device's aux state before the write, if it was a device.
    u64 device_aux_before = 0;
    /// The whole pre-image word at the aligned address. Storing the full word
    /// rather than just the changed bytes makes undoing an sb or sh trivially
    /// correct, with no byte-mask bookkeeping.
    u32 mem_old = 0;
};

ExecResult execute(CpuState& cpu, Bus& bus, const isa::DecodedInstr& instr,
                   EbreakBehavior ebreak_behavior);

}  // namespace rv::core
