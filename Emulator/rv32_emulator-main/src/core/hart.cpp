#include "core/hart.hpp"

#include "core/csrfile.hpp"
#include "isa/expand.hpp"

namespace rv::core {
namespace {

constexpr CsrAddr kMstatus = 0x300;
constexpr CsrAddr kMie = 0x304;
constexpr CsrAddr kMtvec = 0x305;
constexpr CsrAddr kMip = 0x344;
constexpr CsrAddr kMepc = 0x341;
constexpr CsrAddr kMcause = 0x342;
constexpr CsrAddr kMtval = 0x343;
constexpr CsrAddr kMcycle = 0xb00;
constexpr CsrAddr kMinstret = 0xb02;
constexpr CsrAddr kMcycleh = 0xb80;
constexpr CsrAddr kMinstreth = 0xb82;

}  // namespace

Hart::Hart(u32 imem_size, u32 dmem_size) : imem_(0, imem_size), bus_(dmem_size) {}

void Hart::reset(Addr entry) {
    cpu_.reset(entry);
    bus_.reset();
    halt_reason_ = HaltReason::None;
}

void Hart::take_trap(TrapCause cause, u32 tval, Addr epc, StepOutcome& out) {
    out.trapped = true;
    out.cause = cause;
    out.tval = tval;
    out.touched_many_csrs = true;  // mepc, mcause, mtval and mstatus all change

    const u32 mtvec = cpu_.csr.read(kMtvec);
    if (mtvec == 0) {
        halt_reason_ = HaltReason::UnhandledTrap;
        out.halt = halt_reason_;
        out.pc_after = cpu_.pc;  // stay put, so the UI shows the faulting instruction
        return;
    }

    cpu_.csr.raw_write(kMepc, epc);
    cpu_.csr.raw_write(kMcause, static_cast<u32>(cause));
    cpu_.csr.raw_write(kMtval, tval);

    // Save the interrupt-enable bit into MPIE, disable interrupts, and record
    // machine mode as the previous privilege level.
    const u32 mstatus = cpu_.csr.read(kMstatus);
    u32 next = mstatus & ~0x1888u;
    next |= bit(mstatus, 3) << 7;  // MPIE <- MIE
    next |= 3u << 11;              // MPP  <- machine mode
    cpu_.csr.raw_write(kMstatus, next);

    cpu_.pc = mtvec & ~3u;  // direct mode
    out.pc_after = cpu_.pc;
}

std::optional<InterruptCause> Hart::pending_interrupt() const {
    const u32 mstatus = cpu_.csr.read(kMstatus);
    if (bit(mstatus, 3) == 0) return std::nullopt;  // MIE clear: globally masked

    const u32 active = cpu_.csr.read(kMie) & cpu_.csr.read(kMip) & kIrqAll;
    if (active == 0) return std::nullopt;

    // Priority is fixed by the spec.
    if ((active & kIrqExternal) != 0) return InterruptCause::MachineExternal;
    if ((active & kIrqSoftware) != 0) return InterruptCause::MachineSoftware;
    return InterruptCause::MachineTimer;
}

void Hart::take_interrupt(InterruptCause cause, StepOutcome& out) {
    out.interrupted = true;
    out.interrupt = cause;
    out.touched_many_csrs = true;

    const u32 mtvec = cpu_.csr.read(kMtvec);
    if (mtvec == 0) {
        // Enabling an interrupt without installing a handler is a mistake
        // worth naming, not a jump to address zero.
        halt_reason_ = HaltReason::UnhandledTrap;
        out.halt = halt_reason_;
        out.trapped = true;
        return;
    }

    cpu_.csr.raw_write(kMepc, cpu_.pc);
    cpu_.csr.raw_write(kMcause, kMcauseInterrupt | static_cast<u32>(cause));
    cpu_.csr.raw_write(kMtval, 0);

    const u32 mstatus = cpu_.csr.read(kMstatus);
    u32 next = mstatus & ~0x1888u;
    next |= bit(mstatus, 3) << 7;  // MPIE <- MIE
    next |= 3u << 11;              // MPP  <- machine mode
    cpu_.csr.raw_write(kMstatus, next);

    cpu_.pc = mtvec & ~3u;  // direct mode
    out.pc_after = cpu_.pc;
}

StepOutcome Hart::step() {
    StepOutcome out;
    out.pc_before = cpu_.pc;
    out.pc_after = cpu_.pc;
    out.cycle_before = cpu_.cycle;
    out.instret_before = cpu_.instret;

    out.csr_before = cpu_.csr;

    if (halted()) {
        out.halt = halt_reason_;
        return out;
    }

    // Make the counter CSRs observable at their current values before the
    // instruction runs, so `csrr t0, cycle` sees the cycle it is executing in,
    // and publish whatever the devices are asserting on mip.
    cpu_.csr.set_counters(cpu_.cycle, cpu_.instret);
    cpu_.csr.set_pending_interrupts(bus_.pending_interrupts(cpu_.cycle));

    // Interrupts are checked before the fetch, so the instruction at pc has
    // not run yet and mepc points at it -- mret resumes it rather than
    // skipping it. That is the difference from an exception, and it is why
    // handlers do not adjust mepc the way an ecall handler must.
    if (const auto interrupt = pending_interrupt()) {
        take_interrupt(*interrupt, out);
        ++cpu_.cycle;
        bus_.set_now(cpu_.cycle);
        return out;
    }

    // ---- fetch (from IMEM, never from the data bus) ----
    // With the C extension present IALIGN is 16, so only an odd pc faults.
    if ((cpu_.pc & 1u) != 0) {
        take_trap(TrapCause::InstructionAddressMisaligned, cpu_.pc, cpu_.pc, out);
        return out;
    }
    // The width is in the first halfword, so it has to be read before we know
    // how much to bounds-check. Fetching in two halves also lets a 32-bit
    // instruction straddle a word boundary, which it now can: once code is a
    // halfword stream, nothing keeps four-byte instructions on four-byte
    // addresses.
    if (!imem_.contains(cpu_.pc) || !imem_.contains(cpu_.pc + 1)) {
        take_trap(TrapCause::InstructionAccessFault, cpu_.pc, cpu_.pc, out);
        return out;
    }
    const u16 low = imem_.read_half_raw(cpu_.pc);
    if (isa::instruction_length(low) == 4) {
        if (!imem_.contains(cpu_.pc + 2) || !imem_.contains(cpu_.pc + 3)) {
            take_trap(TrapCause::InstructionAccessFault, cpu_.pc, cpu_.pc, out);
            return out;
        }
        out.raw = static_cast<Word>(low) |
                  (static_cast<Word>(imem_.read_half_raw(cpu_.pc + 2)) << 16);
    } else {
        out.raw = low;
    }

    // ---- decode ----
    out.instr = isa::decode(out.raw);
    if (!out.instr.valid()) {
        take_trap(TrapCause::IllegalInstruction, out.raw, cpu_.pc, out);
        ++cpu_.cycle;
        bus_.set_now(cpu_.cycle);
        return out;
    }

    // ---- execute ----
    // A compressed instruction is an alias for a base one, so it is rewritten
    // into that base instruction and the executor runs the result. exec.cpp has
    // no compressed cases at all; see isa/expand.hpp for why that is the point
    // rather than a shortcut. `out.instr` keeps the compressed decode, because
    // that is what the disassembler, the trace and the encoding panel should
    // show -- the program said `c.addi`, not `addi`.
    const ExecResult result =
        execute(cpu_, bus_, isa::expand(out.instr), ebreak_behavior_);
    out.reg_written = result.reg_written;
    out.reg_old = result.reg_old;
    out.csr_written = result.csr_written;
    out.wrote_csr = result.wrote_csr;
    out.csr_old = result.csr_old;
    out.mem_written = result.mem_written;
    out.mem_width = result.mem_width;
    out.mem_old = result.mem_old;

    // If the write landed on a device, remember whatever its peek/poke pair
    // cannot restore.
    if (result.mem_written != kNoAddr) {
        if (Device* device = bus_.device_for_address(result.mem_written)) {
            out.device_slot = device->slot();
            out.device_aux_before = result.device_aux_before;
        }
    }
    // mret rewrites mstatus wholesale alongside jumping, so it needs the same
    // full-snapshot treatment as trap entry.
    if (out.instr.id == isa::InstrId::MRET) out.touched_many_csrs = true;

    // The machine counters are writable, so a csrw to one has to move the
    // hart's own counter -- otherwise the next set_counters() would overwrite
    // the value the program just stored and the write would appear to do
    // nothing. Reading both halves back is simpler than tracking which half was
    // written, and costs nothing on the overwhelmingly common path where no CSR
    // was touched at all.
    if (result.wrote_csr) {
        switch (result.csr_written) {
            case kMcycle:
            case kMcycleh:
                cpu_.cycle = cpu_.csr.machine_cycle();
                break;
            case kMinstret:
            case kMinstreth:
                cpu_.instret = cpu_.csr.machine_instret();
                break;
            default:
                break;
        }
    }

    // wfi asks to skip ahead to the next timer event. The increment lands after
    // any write above, so an instruction that zeroes mcycle still retires and
    // still counts.
    cpu_.cycle += 1 + result.advance_cycles;
    bus_.set_now(cpu_.cycle);

    if (result.trapped) {
        take_trap(result.cause, result.tval, cpu_.pc, out);
        return out;
    }

    if (result.halt != HaltReason::None) {
        halt_reason_ = result.halt;
        out.halt = result.halt;
        // Retire the ebreak and leave the pc past it, so that resuming after a
        // breakpoint does not re-trigger the same halt.
        ++cpu_.instret;
        cpu_.pc = result.next_pc;
        out.pc_after = cpu_.pc;
        return out;
    }

    ++cpu_.instret;
    cpu_.pc = result.next_pc;
    out.pc_after = cpu_.pc;
    return out;
}

}  // namespace rv::core
