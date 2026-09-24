// The machine: one hart, its instruction memory, and its data bus.
#pragma once

#include <optional>

#include "core/bus.hpp"
#include "core/cpustate.hpp"
#include "core/exec.hpp"
#include "core/memory.hpp"
#include "core/trap.hpp"
#include "isa/decode.hpp"

namespace rv::core {

inline constexpr u32 kDefaultImemSize = 16 * 1024;
inline constexpr u32 kDefaultDmemSize = 16 * 1024;

/// Everything that happened in one instruction. Rich enough that the debugger
/// never has to ask a second question about the step it just took.
struct StepOutcome {
    Addr pc_before = 0;
    Addr pc_after = 0;
    Word raw = 0;
    isa::DecodedInstr instr;

    bool trapped = false;
    TrapCause cause = TrapCause::None;
    u32 tval = 0;

    /// Set when the step was pre-empted by an interrupt rather than executing
    /// the instruction at pc_before. `interrupt` says which line fired.
    bool interrupted = false;
    InterruptCause interrupt = InterruptCause::MachineTimer;

    HaltReason halt = HaltReason::None;

    // What changed, and what was there before -- see ExecResult for why this
    // is cheap to collect and what it is for.
    RegIdx reg_written = kNoReg;
    u32 reg_old = 0;

    CsrAddr csr_written = 0;
    bool wrote_csr = false;
    u32 csr_old = 0;

    Addr mem_written = kNoAddr;
    u8 mem_width = 0;
    u32 mem_old = 0;

    /// Set when the step entered a trap or interrupt handler, or returned from
    /// one. Those touch several CSRs at once, which does not fit the
    /// single-CSR record; the history keeps `csr_before` instead. Traps are
    /// rare enough that widening the common case for them would be the wrong
    /// trade.
    bool touched_many_csrs = false;
    /// The CSR file as it was *before* the step. Captured unconditionally
    /// because whether the step would touch several CSRs is not known until
    /// afterwards -- and reconstructing it from the post-step state is exactly
    /// the bug this field exists to prevent.
    CsrFile csr_before;

    u64 cycle_before = 0;
    u64 instret_before = 0;

    /// The device this step wrote to, and the state a peek/poke pair cannot
    /// express -- a UART's output length, a computing peripheral's start cycle
    /// and last result. Everything else a device write changes is covered by
    /// the ordinary one-word memory delta, because devices implement
    /// side-effect-free peek and poke.
    std::size_t device_slot = kNoDevice;
    u64 device_aux_before = 0;

    static constexpr std::size_t kNoDevice = ~std::size_t{0};
};

class Hart {
public:
    Hart(u32 imem_size = kDefaultImemSize, u32 dmem_size = kDefaultDmemSize);

    /// Change how much memory the machine has. Contents that still fit are
    /// kept; a program too big for a shrunk imem is truncated, which is the
    /// same thing that would have happened had it been assembled at this size.
    void resize_memories(u32 imem_size, u32 dmem_size) {
        imem_.resize(imem_size);
        bus_.resize_dmem(dmem_size);
    }

    /// Place IMEM/DMEM at absolute bases (uBee SoC: 0x80000000 / 0x20000000).
    void set_memory_bases(Addr imem_base, Addr dmem_base) {
        imem_.set_base(imem_base);
        bus_.set_dmem_base(dmem_base);
    }

    /// Default reset/entry PC from a board file. Used when a program has no
    /// `_start` label. Zero means "leave the caller's entry alone".
    void set_reset_entry(Addr entry) { reset_entry_ = entry; }
    Addr reset_entry() const { return reset_entry_; }

    CpuState& cpu() { return cpu_; }
    const CpuState& cpu() const { return cpu_; }
    Memory& imem() { return imem_; }
    const Memory& imem() const { return imem_; }
    Bus& bus() { return bus_; }
    const Bus& bus() const { return bus_; }

    /// Clear architectural state and start at `entry`. Instruction memory is
    /// left alone -- resetting a core does not erase its ROM.
    void reset(Addr entry = 0);

    /// Fetch, decode, execute, and commit. Stepping a halted hart is a no-op
    /// that reports the existing halt reason.
    StepOutcome step();

    bool halted() const { return halt_reason_ != HaltReason::None; }
    HaltReason halt_reason() const { return halt_reason_; }
    void clear_halt() { halt_reason_ = HaltReason::None; }

    EbreakBehavior ebreak_behavior() const { return ebreak_behavior_; }
    void set_ebreak_behavior(EbreakBehavior behavior) { ebreak_behavior_ = behavior; }

private:
    /// Enter the machine trap handler. If mtvec is still zero there is nowhere
    /// to go, so the hart halts instead of jumping to address zero and
    /// executing whatever happens to be there -- silently re-running the
    /// program from the top is the single most confusing failure mode for a
    /// beginner.
    void take_trap(TrapCause cause, u32 tval, Addr epc, StepOutcome& out);

    /// Transfer to the handler for a pending interrupt. Unlike an exception,
    /// mepc points at the instruction that has *not* run yet, so mret resumes
    /// it rather than skipping it.
    void take_interrupt(InterruptCause cause, StepOutcome& out);

    /// The highest-priority enabled interrupt that is pending, if any. Order is
    /// fixed by the spec: external, then software, then timer.
    std::optional<InterruptCause> pending_interrupt() const;

    CpuState cpu_;
    Memory imem_;
    Bus bus_;
    Addr reset_entry_ = 0;
    EbreakBehavior ebreak_behavior_ = EbreakBehavior::HaltToDebugger;
    HaltReason halt_reason_ = HaltReason::None;
};

}  // namespace rv::core
