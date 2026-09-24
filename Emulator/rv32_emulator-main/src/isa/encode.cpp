#include "isa/encode.hpp"

#include "isa/csr.hpp"
#include "isa/fields.hpp"
#include "isa/regnames.hpp"

namespace rv::isa {
namespace {

EncodeResult range_error(EncodeError error, const char* field, i64 value, i64 min, i64 max) {
    EncodeResult result;
    result.error = error;
    result.field = field;
    result.value = value;
    result.min = min;
    result.max = max;
    return result;
}

EncodeResult simple_error(EncodeError error, const char* field, i64 value) {
    EncodeResult result;
    result.error = error;
    result.field = field;
    result.value = value;
    return result;
}

}  // namespace

EncodeResult encode(InstrId id, const Operands& operands) {
    const InstrDesc& desc = describe(id);

    if (operands.rd >= kNumRegs || operands.rs1 >= kNumRegs || operands.rs2 >= kNumRegs) {
        return range_error(EncodeError::RegisterOutOfRange, "register", operands.rd, 0,
                           kNumRegs - 1);
    }

    EncodeResult result;
    result.word = desc.match;
    result.size_bytes = desc.length();
    const i64 imm = operands.imm;

    // Shared checks for the compressed forms. Each returns a populated error or
    // leaves `result` alone, so the per-syntax cases below stay readable.
    const auto need_compressible = [&](RegIdx reg, const char* field) {
        return isa::is_compressible_reg(reg)
                   ? EncodeError::None
                   : (result = simple_error(EncodeError::RegisterNotCompressible, field, reg),
                      EncodeError::RegisterNotCompressible);
    };
    const auto need_nonzero_reg = [&](RegIdx reg, const char* field) {
        return reg != 0 ? EncodeError::None
                        : (result = simple_error(EncodeError::RegisterMustNotBeZero, field, reg),
                           EncodeError::RegisterMustNotBeZero);
    };
    /// Range, alignment and non-zero-ness for a scaled compressed immediate.
    ///
    /// On a step failure both `min` and `max` carry the step rather than a
    /// range, because "must be a multiple of 4" has no useful bounds to report.
    /// The assembler tells the two apart by `min > 1`, since the base formats
    /// raise the same error with min == max == 0.
    const auto check_imm = [&](i64 value, i64 lo, i64 hi, i64 step, bool nonzero,
                               const char* field) {
        if (nonzero && value == 0) {
            result = simple_error(EncodeError::ImmediateMustNotBeZero, field, value);
            return false;
        }
        if (value < lo || value > hi) {
            result = range_error(EncodeError::ImmediateOutOfRange, field, value, lo, hi);
            return false;
        }
        if (step > 1 && (value % step) != 0) {
            result = range_error(EncodeError::ImmediateMisaligned, field, value, step, step);
            return false;
        }
        return true;
    };

    switch (desc.syntax) {
        case OperandSyntax::NONE:
            break;

        case OperandSyntax::RD_RS1_RS2:
            result.word |= put_rd(operands.rd) | put_rs1(operands.rs1) | put_rs2(operands.rs2);
            break;

        case OperandSyntax::RD_RS1_IMM:
        case OperandSyntax::RD_OFS_RS1:
            if (!fits_signed(imm, 12)) {
                return range_error(EncodeError::ImmediateOutOfRange, "immediate", imm, -2048, 2047);
            }
            result.word |= put_rd(operands.rd) | put_rs1(operands.rs1) | put_imm_i(operands.imm);
            break;

        case OperandSyntax::RD_RS1_SHAMT:
            if (imm < 0 || imm > 31) {
                return range_error(EncodeError::ShiftAmountOutOfRange, "shift amount", imm, 0, 31);
            }
            result.word |= put_rd(operands.rd) | put_rs1(operands.rs1) |
                           put_shamt(static_cast<u32>(imm));
            break;

        case OperandSyntax::RS2_OFS_RS1:
            if (!fits_signed(imm, 12)) {
                return range_error(EncodeError::ImmediateOutOfRange, "offset", imm, -2048, 2047);
            }
            result.word |= put_rs1(operands.rs1) | put_rs2(operands.rs2) | put_imm_s(operands.imm);
            break;

        case OperandSyntax::RS1_RS2_LBL:
            if ((imm & 1) != 0) {
                return range_error(EncodeError::ImmediateMisaligned, "branch offset", imm, 0, 0);
            }
            if (!fits_signed(imm, 13)) {
                return range_error(EncodeError::ImmediateOutOfRange, "branch offset", imm, -4096,
                                   4094);
            }
            result.word |= put_rs1(operands.rs1) | put_rs2(operands.rs2) | put_imm_b(operands.imm);
            break;

        case OperandSyntax::RD_UIMM:
            if ((static_cast<u32>(operands.imm) & 0xfffu) != 0) {
                return range_error(EncodeError::ImmediateNotShifted, "upper immediate", imm, 0, 0);
            }
            result.word |= put_rd(operands.rd) | put_imm_u(operands.imm);
            break;

        case OperandSyntax::RD_LBL:
            if ((imm & 1) != 0) {
                return range_error(EncodeError::ImmediateMisaligned, "jump offset", imm, 0, 0);
            }
            if (!fits_signed(imm, 21)) {
                return range_error(EncodeError::ImmediateOutOfRange, "jump offset", imm, -1048576,
                                   1048574);
            }
            result.word |= put_rd(operands.rd) | put_imm_j(operands.imm);
            break;

        case OperandSyntax::RD_CSR_RS1:
            if (operands.csr > 0xfff) {
                return range_error(EncodeError::CsrOutOfRange, "csr", operands.csr, 0, 0xfff);
            }
            result.word |= put_rd(operands.rd) | put_rs1(operands.rs1) | put_csr(operands.csr);
            break;

        case OperandSyntax::RD_CSR_ZIMM:
            if (operands.csr > 0xfff) {
                return range_error(EncodeError::CsrOutOfRange, "csr", operands.csr, 0, 0xfff);
            }
            if (imm < 0 || imm > 31) {
                return range_error(EncodeError::ImmediateOutOfRange, "immediate", imm, 0, 31);
            }
            result.word |= put_rd(operands.rd) | put_zimm(static_cast<u32>(imm)) |
                           put_csr(operands.csr);
            break;

        // ---- compressed ----------------------------------------------------
        // Every case here is doing the same job: prove the operands fit the
        // narrower field, then scatter them. The proofs are the interesting
        // half -- they are what turns "that does not encode" into a diagnostic
        // naming the register or the range.
        case OperandSyntax::C_RD_IMM:  // c.addi, c.li
            // rd == x0 is a HINT here, not an error: it encodes fine and
            // executes as a nop, so the encoder allows it and the assembler
            // decides separately whether a human should be writing it.
            if (!check_imm(imm, -32, 31, 1, false, "immediate")) return result;
            result.word |= put_crd(operands.rd) | put_c_imm_ci(operands.imm);
            break;

        case OperandSyntax::C_RD_UIMM: {  // c.lui
            if (need_nonzero_reg(operands.rd, "rd") != EncodeError::None) return result;
            if (operands.rd == 2) {
                // rd == 2 is c.addi16sp's encoding, not a c.lui at all.
                result = simple_error(EncodeError::RegisterOutOfRange, "rd", operands.rd);
                return result;
            }
            if ((static_cast<u32>(operands.imm) & 0xfffu) != 0) {
                result = simple_error(EncodeError::ImmediateNotShifted, "upper immediate", imm);
                return result;
            }
            const i64 upper = imm >> 12;
            if (!check_imm(upper, -32, 31, 1, true, "upper immediate")) return result;
            result.word |= put_crd(operands.rd) | put_c_imm_lui(operands.imm);
            break;
        }

        case OperandSyntax::C_RD_SHAMT:  // c.slli
            // shamt == 0 and rd == x0 are both HINTs. RV32 has no shamt[5], so
            // 32 and up are the only genuinely unencodable amounts.
            if (imm < 0 || imm > 31) {
                result = range_error(EncodeError::ShiftAmountOutOfRange, "shift amount", imm, 0, 31);
                return result;
            }
            result.word |= put_crd(operands.rd) | put_c_shamt(static_cast<u32>(imm));
            break;

        case OperandSyntax::C_RD_RS2:  // c.mv, c.add
            // rd == x0 is a HINT; rs2 == x0 is not, because those bits are
            // c.jr/c.jalr and the result would decode as a different
            // instruction entirely.
            if (need_nonzero_reg(operands.rs2, "rs2") != EncodeError::None) return result;
            result.word |= put_crd(operands.rd) | put_crs2(operands.rs2);
            break;

        case OperandSyntax::C_RDP_IMM:  // c.andi
            if (need_compressible(operands.rd, "rd") != EncodeError::None) return result;
            if (!check_imm(imm, -32, 31, 1, false, "immediate")) return result;
            result.word |= put_crs1p(operands.rd) | put_c_imm_ci(operands.imm);
            break;

        case OperandSyntax::C_RDP_UIMM:  // c.addi4spn
            if (need_compressible(operands.rd, "rd") != EncodeError::None) return result;
            if (!check_imm(imm, 0, 1020, 4, true, "immediate")) return result;
            result.word |= put_crs2p(operands.rd) | put_c_imm_addi4spn(static_cast<u32>(imm));
            break;

        case OperandSyntax::C_RDP_SHAMT:  // c.srli, c.srai
            if (need_compressible(operands.rd, "rd") != EncodeError::None) return result;
            if (imm < 0 || imm > 31) {
                result = range_error(EncodeError::ShiftAmountOutOfRange, "shift amount", imm, 0, 31);
                return result;
            }
            result.word |= put_crs1p(operands.rd) | put_c_shamt(static_cast<u32>(imm));
            break;

        case OperandSyntax::C_RDP_RS2P:  // c.sub, c.xor, c.or, c.and
            if (need_compressible(operands.rd, "rd") != EncodeError::None) return result;
            if (need_compressible(operands.rs2, "rs2") != EncodeError::None) return result;
            result.word |= put_crs1p(operands.rd) | put_crs2p(operands.rs2);
            break;

        case OperandSyntax::C_RS1:  // c.jr, c.jalr
            if (need_nonzero_reg(operands.rs1, "rs1") != EncodeError::None) return result;
            result.word |= put_crd(operands.rs1);
            break;

        case OperandSyntax::C_IMM:  // c.addi16sp
            if (!check_imm(imm, -512, 496, 16, true, "immediate")) return result;
            result.word |= put_c_imm_addi16sp(operands.imm);
            break;

        case OperandSyntax::C_LBL:  // c.j, c.jal
            if (!check_imm(imm, -2048, 2046, 2, false, "jump offset")) return result;
            result.word |= put_c_imm_cj(operands.imm);
            break;

        case OperandSyntax::C_RS1P_LBL:  // c.beqz, c.bnez
            if (need_compressible(operands.rs1, "rs1") != EncodeError::None) return result;
            if (!check_imm(imm, -256, 254, 2, false, "branch offset")) return result;
            result.word |= put_crs1p(operands.rs1) | put_c_imm_cb(operands.imm);
            break;

        case OperandSyntax::C_RDP_OFS_RS1P:  // c.lw
            if (need_compressible(operands.rd, "rd") != EncodeError::None) return result;
            if (need_compressible(operands.rs1, "rs1") != EncodeError::None) return result;
            if (!check_imm(imm, 0, 124, 4, false, "offset")) return result;
            result.word |= put_crs2p(operands.rd) | put_crs1p(operands.rs1) |
                           put_c_imm_lw(static_cast<u32>(imm));
            break;

        case OperandSyntax::C_RS2P_OFS_RS1P:  // c.sw
            if (need_compressible(operands.rs2, "rs2") != EncodeError::None) return result;
            if (need_compressible(operands.rs1, "rs1") != EncodeError::None) return result;
            if (!check_imm(imm, 0, 124, 4, false, "offset")) return result;
            result.word |= put_crs2p(operands.rs2) | put_crs1p(operands.rs1) |
                           put_c_imm_lw(static_cast<u32>(imm));
            break;

        case OperandSyntax::C_RD_OFS_SP:  // c.lwsp
            if (need_nonzero_reg(operands.rd, "rd") != EncodeError::None) return result;
            if (!check_imm(imm, 0, 252, 4, false, "offset")) return result;
            result.word |= put_crd(operands.rd) | put_c_imm_lwsp(static_cast<u32>(imm));
            break;

        case OperandSyntax::C_RS2_OFS_SP:  // c.swsp
            if (!check_imm(imm, 0, 252, 4, false, "offset")) return result;
            result.word |= put_crs2(operands.rs2) | put_c_imm_swsp(static_cast<u32>(imm));
            break;
    }

    return result;
}

Operands operands_of(const DecodedInstr& instr) {
    Operands operands;
    operands.rd = instr.rd;
    operands.rs1 = instr.rs1;
    operands.rs2 = instr.rs2;
    operands.imm = instr.imm;
    operands.csr = instr.csr;
    return operands;
}

const char* encode_error_message(EncodeError error) {
    switch (error) {
        case EncodeError::None: return "ok";
        case EncodeError::ImmediateOutOfRange: return "immediate out of range";
        case EncodeError::ImmediateMisaligned: return "target must be 2-byte aligned";
        case EncodeError::ImmediateNotShifted: return "upper immediate must have zero low 12 bits";
        case EncodeError::ShiftAmountOutOfRange: return "shift amount out of range";
        case EncodeError::CsrOutOfRange: return "csr address out of range";
        case EncodeError::RegisterOutOfRange: return "register number out of range";
        case EncodeError::RegisterNotCompressible: return "register not reachable from this form";
        case EncodeError::RegisterMustNotBeZero: return "this form cannot use zero";
        case EncodeError::ImmediateMustNotBeZero: return "this form cannot encode zero";
    }
    return "unknown encoding error";
}

}  // namespace rv::isa
