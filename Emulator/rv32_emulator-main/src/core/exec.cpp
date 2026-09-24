#include "core/exec.hpp"

#include <limits>

#include "isa/csr.hpp"

namespace rv::core {

ExecResult execute(CpuState& cpu, Bus& bus, const isa::DecodedInstr& instr,
                   EbreakBehavior ebreak_behavior) {
    using isa::InstrId;

    ExecResult out;
    // A compressed instruction advances the pc by 2, not 4. Seeding next_pc
    // from the decoded length covers every instruction that does not branch,
    // which is most of them, in one place.
    out.next_pc = cpu.pc + instr.length;

    const u32 rs1 = cpu.reg(instr.rs1);
    const u32 rs2 = cpu.reg(instr.rs2);
    const u32 imm = static_cast<u32>(instr.imm);

    const auto raise = [&](TrapCause cause, u32 tval) {
        out.trapped = true;
        out.cause = cause;
        out.tval = tval;
    };

    const auto write_reg = [&](u32 value) {
        if (instr.rd != 0) {
            out.reg_written = instr.rd;
            out.reg_old = cpu.x[instr.rd];  // captured before the write
        }
        cpu.set_reg(instr.rd, value);
    };

    const auto do_load = [&](u8 width, bool is_signed) {
        const Addr addr = rs1 + imm;
        const MemResult result = bus.load(addr, width, is_signed);
        if (!result.ok) return raise(result.cause, addr);
        write_reg(result.value);
    };

    const auto do_store = [&](u8 width) {
        const Addr addr = rs1 + imm;
        const Addr aligned = addr & ~3u;
        const u32 before = bus.peek_word(aligned);
        // A device may hold state no register can express; capture it before
        // the write rather than trying to reconstruct it after.
        const Device* target = bus.device_for_address(aligned);
        const u64 aux_before = target != nullptr ? target->aux_state() : 0u;

        const MemResult result = bus.store(addr, width, rs2);
        if (!result.ok) return raise(result.cause, addr);
        out.mem_written = aligned;
        out.mem_width = width;
        out.mem_old = before;
        out.device_aux_before = aux_before;
    };

    const auto branch_if = [&](bool taken) {
        if (taken) out.next_pc = cpu.pc + imm;
    };

    // Shared by all six Zicsr instructions. `write_value` is what would be
    // written; `should_write` encodes the spec's rule that csrrs/csrrc with a
    // zero source register (and csrrsi/csrrci with a zero immediate) must not
    // write at all, so that they stay side-effect-free reads.
    const auto csr_op = [&](u32 write_value, bool should_write, bool should_read) {
        if (!cpu.csr.exists(instr.csr)) return raise(TrapCause::IllegalInstruction, instr.raw);
        if (should_write && isa::csr_is_read_only(instr.csr)) {
            return raise(TrapCause::IllegalInstruction, instr.raw);
        }
        const u32 old = should_read ? cpu.csr.read(instr.csr) : 0;
        if (should_write) {
            out.wrote_csr = true;
            out.csr_written = instr.csr;
            out.csr_old = cpu.csr.read(instr.csr);
            cpu.csr.write(instr.csr, write_value);
        }
        if (should_read) write_reg(old);
    };

    switch (instr.id) {
        // ---- upper immediates ----------------------------------------------
        case InstrId::LUI:
            write_reg(imm);
            break;
        case InstrId::AUIPC:
            // Relative to the address of the auipc itself, not pc+4.
            write_reg(cpu.pc + imm);
            break;

        // ---- jumps ---------------------------------------------------------
        case InstrId::JAL: {
            const Addr target = cpu.pc + imm;
            write_reg(cpu.pc + instr.length);
            out.next_pc = target;
            break;
        }
        case InstrId::JALR: {
            // Two details the spec is explicit about and implementations get
            // wrong: the low bit of the computed target is cleared, and the
            // target must be computed before the link register is written so
            // that `jalr ra, ra, 0` jumps to the old ra.
            const Addr target = (rs1 + imm) & ~1u;
            write_reg(cpu.pc + instr.length);
            out.next_pc = target;
            break;
        }

        // ---- branches ------------------------------------------------------
        case InstrId::BEQ: branch_if(rs1 == rs2); break;
        case InstrId::BNE: branch_if(rs1 != rs2); break;
        case InstrId::BLT: branch_if(static_cast<i32>(rs1) < static_cast<i32>(rs2)); break;
        case InstrId::BGE: branch_if(static_cast<i32>(rs1) >= static_cast<i32>(rs2)); break;
        case InstrId::BLTU: branch_if(rs1 < rs2); break;
        case InstrId::BGEU: branch_if(rs1 >= rs2); break;

        // ---- loads and stores ----------------------------------------------
        case InstrId::LB: do_load(1, true); break;
        case InstrId::LH: do_load(2, true); break;
        case InstrId::LW: do_load(4, false); break;
        case InstrId::LBU: do_load(1, false); break;
        case InstrId::LHU: do_load(2, false); break;
        case InstrId::SB: do_store(1); break;
        case InstrId::SH: do_store(2); break;
        case InstrId::SW: do_store(4); break;

        // ---- register-immediate --------------------------------------------
        case InstrId::ADDI: write_reg(rs1 + imm); break;
        case InstrId::SLTI:
            write_reg(static_cast<i32>(rs1) < instr.imm ? 1u : 0u);
            break;
        case InstrId::SLTIU:
            // The immediate is sign-extended first, then compared as unsigned.
            // That is what makes `sltiu rd, rs1, 1` mean "rs1 == 0".
            write_reg(rs1 < imm ? 1u : 0u);
            break;
        case InstrId::XORI: write_reg(rs1 ^ imm); break;
        case InstrId::ORI: write_reg(rs1 | imm); break;
        case InstrId::ANDI: write_reg(rs1 & imm); break;
        case InstrId::SLLI: write_reg(shift_left(rs1, imm)); break;
        case InstrId::SRLI: write_reg(shift_right_logical(rs1, imm)); break;
        case InstrId::SRAI: write_reg(shift_right_arith(rs1, imm)); break;

        // ---- register-register ---------------------------------------------
        case InstrId::ADD: write_reg(rs1 + rs2); break;
        case InstrId::SUB: write_reg(rs1 - rs2); break;
        case InstrId::SLL: write_reg(shift_left(rs1, rs2)); break;
        case InstrId::SLT:
            write_reg(static_cast<i32>(rs1) < static_cast<i32>(rs2) ? 1u : 0u);
            break;
        case InstrId::SLTU: write_reg(rs1 < rs2 ? 1u : 0u); break;
        case InstrId::XOR: write_reg(rs1 ^ rs2); break;
        case InstrId::SRL: write_reg(shift_right_logical(rs1, rs2)); break;
        case InstrId::SRA: write_reg(shift_right_arith(rs1, rs2)); break;
        case InstrId::OR: write_reg(rs1 | rs2); break;
        case InstrId::AND: write_reg(rs1 & rs2); break;

        // ---- M extension ----------------------------------------------------
        // All products are formed in 64 bits and then narrowed; the three
        // high-half variants differ only in how the operands are extended.
        case InstrId::MUL:
            write_reg(static_cast<u32>(static_cast<u64>(rs1) * static_cast<u64>(rs2)));
            break;
        case InstrId::MULH: {
            const i64 product = static_cast<i64>(static_cast<i32>(rs1)) *
                                static_cast<i64>(static_cast<i32>(rs2));
            write_reg(static_cast<u32>(static_cast<u64>(product) >> 32));
            break;
        }
        case InstrId::MULHSU: {
            const i64 product =
                static_cast<i64>(static_cast<i32>(rs1)) * static_cast<i64>(static_cast<u64>(rs2));
            write_reg(static_cast<u32>(static_cast<u64>(product) >> 32));
            break;
        }
        case InstrId::MULHU: {
            const u64 product = static_cast<u64>(rs1) * static_cast<u64>(rs2);
            write_reg(static_cast<u32>(product >> 32));
            break;
        }
        case InstrId::DIV: {
            // Division never traps in RISC-V; both edge cases have defined
            // results. Note that computing INT32_MIN / -1 with the native
            // operator is undefined behaviour in C++, so it must be special
            // cased rather than merely corrected afterwards.
            const i32 dividend = static_cast<i32>(rs1);
            const i32 divisor = static_cast<i32>(rs2);
            if (divisor == 0) {
                write_reg(0xffff'ffffu);
            } else if (dividend == std::numeric_limits<i32>::min() && divisor == -1) {
                write_reg(static_cast<u32>(std::numeric_limits<i32>::min()));
            } else {
                write_reg(static_cast<u32>(dividend / divisor));
            }
            break;
        }
        case InstrId::DIVU:
            write_reg(rs2 == 0 ? 0xffff'ffffu : rs1 / rs2);
            break;
        case InstrId::REM: {
            const i32 dividend = static_cast<i32>(rs1);
            const i32 divisor = static_cast<i32>(rs2);
            if (divisor == 0) {
                write_reg(static_cast<u32>(dividend));
            } else if (dividend == std::numeric_limits<i32>::min() && divisor == -1) {
                write_reg(0);
            } else {
                write_reg(static_cast<u32>(dividend % divisor));
            }
            break;
        }
        case InstrId::REMU:
            write_reg(rs2 == 0 ? rs1 : rs1 % rs2);
            break;

        // ---- Zicsr ----------------------------------------------------------
        case InstrId::CSRRW:
            // A csrrw with rd == x0 must not read, because some CSR reads have
            // side effects. The write always happens.
            csr_op(rs1, true, instr.rd != 0);
            break;
        case InstrId::CSRRS:
            csr_op(cpu.csr.read(instr.csr) | rs1, instr.rs1 != 0, true);
            break;
        case InstrId::CSRRC:
            csr_op(cpu.csr.read(instr.csr) & ~rs1, instr.rs1 != 0, true);
            break;
        case InstrId::CSRRWI:
            csr_op(imm, true, instr.rd != 0);
            break;
        case InstrId::CSRRSI:
            csr_op(cpu.csr.read(instr.csr) | imm, imm != 0, true);
            break;
        case InstrId::CSRRCI:
            csr_op(cpu.csr.read(instr.csr) & ~imm, imm != 0, true);
            break;

        // ---- system ---------------------------------------------------------
        case InstrId::FENCE:
            // Single hart, in-order, no store buffer: architecturally a no-op.
            break;
        case InstrId::FENCEI:
            // Harvard: IMEM is never written, so there is no stale instruction
            // fetch to flush. A no-op that cannot become anything else.
            break;
        case InstrId::ECALL:
            raise(TrapCause::EnvironmentCallFromMMode, 0);
            break;
        case InstrId::EBREAK:
            if (ebreak_behavior == EbreakBehavior::TrapToMtvec) {
                raise(TrapCause::Breakpoint, cpu.pc);
            } else {
                out.halt = HaltReason::Ebreak;
            }
            break;
        case InstrId::MRET: {
            const u32 mstatus = cpu.csr.read(0x300);
            const u32 mpie = bit(mstatus, 7);
            // Restore MIE from MPIE, set MPIE, and drop MPP back to machine
            // mode (the only mode this machine implements).
            u32 next = mstatus & ~0x1888u;
            next |= mpie << 3;
            next |= 1u << 7;
            cpu.csr.raw_write(0x300, next);
            out.next_pc = cpu.csr.read(0x341);
            break;
        }
        case InstrId::WFI: {
            // Time is the cycle counter, so waiting means jumping forward to
            // the next event rather than burning instructions in a spin loop --
            // which also keeps the whole thing deterministic and reversible.
            //
            // wfi wakes on a pending interrupt regardless of mstatus.MIE, so
            // only mie is consulted here.
            const u32 armed = cpu.csr.read(0x304) & kIrqAll;
            const u32 pending = bus.pending_interrupts(cpu.cycle) & armed;
            if (pending != 0) break;  // already awake: retire immediately

            const TimerDevice* timer = bus.timer();
            const bool timer_armed =
                (armed & kIrqTimer) != 0 && timer != nullptr && timer->armed();
            if (!timer_armed) {
                // No timer to fire and nothing pending. Nothing can ever wake
                // the hart, so say so rather than looping forever.
                out.halt = HaltReason::WaitingForever;
                break;
            }
            out.advance_cycles = timer->compare() - cpu.cycle;
            break;
        }

        // ---- compressed ----------------------------------------------------
        // Unreachable: the hart expands every compressed instruction before it
        // gets here, so this function only ever sees base ids. The labels exist
        // so that -Werror=switch still forces a decision when a row is added to
        // the C block of instr_table.def -- the decision being "expand it in
        // isa/expand.cpp", not "implement it again here".
        case InstrId::C_ADDI4SPN:
        case InstrId::C_LW:
        case InstrId::C_SW:
        case InstrId::C_NOP:
        case InstrId::C_ADDI:
        case InstrId::C_JAL:
        case InstrId::C_LI:
        case InstrId::C_ADDI16SP:
        case InstrId::C_LUI:
        case InstrId::C_SRLI:
        case InstrId::C_SRAI:
        case InstrId::C_ANDI:
        case InstrId::C_SUB:
        case InstrId::C_XOR:
        case InstrId::C_OR:
        case InstrId::C_AND:
        case InstrId::C_J:
        case InstrId::C_BEQZ:
        case InstrId::C_BNEZ:
        case InstrId::C_SLLI:
        case InstrId::C_LWSP:
        case InstrId::C_JR:
        case InstrId::C_MV:
        case InstrId::C_EBREAK:
        case InstrId::C_JALR:
        case InstrId::C_ADD:
        case InstrId::C_SWSP:
        case InstrId::Count:
            raise(TrapCause::IllegalInstruction, instr.raw);
            break;
    }

    // Any control transfer that lands off a 2-byte boundary faults. Checking it
    // once here covers jal, jalr, branches and mret in one place.
    //
    // With the C extension present IALIGN is 16, not 32, so a target of 2 mod 4
    // is perfectly legal. In practice this trap has become unreachable from
    // executing code -- jal and branch immediates are always even, and jalr
    // clears the low bit -- but a debugger can still set an odd pc, and the
    // check costs one instruction.
    if (!out.trapped && out.halt == HaltReason::None && (out.next_pc & 1u) != 0) {
        raise(TrapCause::InstructionAddressMisaligned, out.next_pc);
    }

    return out;
}

}  // namespace rv::core
