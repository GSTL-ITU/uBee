// A legal, harmless set of operands for any row in the instruction table.
//
// Several tests want to build one instance of every instruction and do
// something with it -- execute it, disassemble it, round-trip it. Before the C
// extension a default-constructed Operands was good enough for that, because
// every base encoding accepts zeros. Compressed rows do not: their register
// fields reach only x8-x15, several forbid x0, and their immediates are scaled
// and often required to be non-zero.
//
// Rather than have each test grow its own table of exceptions, they share this
// one. It answers "what can this row legally say?", not "what is interesting to
// say with it" -- a test that cares about specific values still writes them out.
#pragma once

#include "isa/encode.hpp"
#include "isa/isa.hpp"

namespace sample {

/// A register both compressible (x8-x15) and non-zero, so it satisfies every
/// compressed register constraint at once. a5 is caller-saved and nothing in
/// the fixtures reads it.
inline constexpr rv::RegIdx kSafeReg = 15;

inline rv::isa::Operands operands_for(rv::isa::InstrId id) {
    using rv::isa::OperandSyntax;
    const rv::isa::InstrDesc& desc = rv::isa::describe(id);

    rv::isa::Operands operands;
    const bool compressed = rv::isa::is_compressed_format(desc.format);
    // Base rows keep the historical choice of rd=31/rs=0, which is what makes
    // the executed instruction harmless: it writes a register nothing reads and
    // reads registers that are zero.
    operands.rd = compressed ? kSafeReg : rv::RegIdx{31};
    operands.rs1 = compressed ? kSafeReg : rv::RegIdx{0};
    operands.rs2 = compressed ? kSafeReg : rv::RegIdx{0};

    switch (desc.syntax) {
        case OperandSyntax::RD_CSR_RS1:
        case OperandSyntax::RD_CSR_ZIMM:
            operands.csr = 0x340;  // mscratch: writable and harmless
            break;
        case OperandSyntax::RD_OFS_RS1:
        case OperandSyntax::RS2_OFS_RS1:
            operands.imm = 0x40;  // inside data memory, word aligned
            break;

        // ---- compressed ----------------------------------------------------
        case OperandSyntax::C_RD_IMM:
        case OperandSyntax::C_RDP_IMM:
        case OperandSyntax::C_RD_SHAMT:
        case OperandSyntax::C_RDP_SHAMT:
            operands.imm = 1;
            break;
        case OperandSyntax::C_RD_UIMM:
            operands.imm = 1 << 12;  // non-zero, and positioned like lui
            break;
        case OperandSyntax::C_RDP_UIMM:
            operands.imm = 4;  // non-zero, a multiple of 4
            break;
        case OperandSyntax::C_IMM:
            operands.imm = 16;  // non-zero, a multiple of 16
            break;
        case OperandSyntax::C_LBL:
        case OperandSyntax::C_RS1P_LBL:
            // A branch to itself. Legal to encode, and in an executing test it
            // is the offset that keeps the pc inside the program.
            operands.imm = 0;
            break;
        case OperandSyntax::C_RDP_OFS_RS1P:
        case OperandSyntax::C_RS2P_OFS_RS1P:
            operands.imm = 0x40;
            break;
        case OperandSyntax::C_RD_OFS_SP:
        case OperandSyntax::C_RS2_OFS_SP:
            operands.imm = 0x40;
            operands.rs1 = 2;  // sp, which these forms always address through
            break;
        case OperandSyntax::C_RS1:
            // c.jr/c.jalr jump to whatever the register holds. Using x1 keeps
            // that a jump to zero rather than to a5's contents.
            operands.rs1 = 1;
            break;

        default:
            break;
    }
    return operands;
}

}  // namespace sample
