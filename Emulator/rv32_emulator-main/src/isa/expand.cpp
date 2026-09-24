#include "isa/expand.hpp"

namespace rv::isa {
namespace {

constexpr RegIdx kZero = 0;
constexpr RegIdx kRa = 1;

}  // namespace

DecodedInstr expand(const DecodedInstr& instr) {
    if (!instr.valid() || !is_compressed_format(instr.desc().format)) return instr;

    // Start from the compressed decode: decode() has already put the register
    // numbers and the immediate where the base instruction wants them for most
    // rows, because it fills rd *and* rs1 for the read-modify-write forms. Only
    // the cases that genuinely differ are rewritten below.
    DecodedInstr out = instr;

    switch (instr.id) {
        // ---- already in base form, only the id changes ---------------------
        case InstrId::C_ADDI:       // addi rd, rd, imm
        case InstrId::C_ADDI4SPN:   // addi rd', sp, nzuimm
        case InstrId::C_ADDI16SP:   // addi sp, sp, nzimm
        case InstrId::C_ANDI:       // andi rd', rd', imm
            out.id = instr.id == InstrId::C_ANDI ? InstrId::ANDI : InstrId::ADDI;
            break;
        case InstrId::C_LUI:
            out.id = InstrId::LUI;
            break;
        case InstrId::C_SLLI:
            out.id = InstrId::SLLI;
            break;
        case InstrId::C_SRLI:
            out.id = InstrId::SRLI;
            break;
        case InstrId::C_SRAI:
            out.id = InstrId::SRAI;
            break;
        case InstrId::C_SUB:
            out.id = InstrId::SUB;
            break;
        case InstrId::C_XOR:
            out.id = InstrId::XOR;
            break;
        case InstrId::C_OR:
            out.id = InstrId::OR;
            break;
        case InstrId::C_AND:
            out.id = InstrId::AND;
            break;
        case InstrId::C_ADD:
            // c.add is rd = rd + rs2, so rs1 is rd -- which decode() did not
            // set, because the C_RD_RS2 syntax is shared with c.mv where rs1
            // must be zero instead.
            out.id = InstrId::ADD;
            out.rs1 = instr.rd;
            break;
        case InstrId::C_LW:
        case InstrId::C_LWSP:
            out.id = InstrId::LW;
            break;
        case InstrId::C_SW:
        case InstrId::C_SWSP:
            out.id = InstrId::SW;
            break;

        // ---- a zero register has to be supplied ---------------------------
        case InstrId::C_NOP:
            // The canonical nop, addi x0, x0, 0. Spelled out rather than left
            // to fall through c.addi, because c.nop has its own encoding.
            out.id = InstrId::ADDI;
            out.rd = kZero;
            out.rs1 = kZero;
            out.imm = 0;
            break;
        case InstrId::C_LI:
            // addi rd, x0, imm -- the source is zero, not rd.
            out.id = InstrId::ADDI;
            out.rs1 = kZero;
            break;
        case InstrId::C_MV:
            // add rd, x0, rs2. Using add rather than addi is what the spec
            // says, and it matters: `c.mv a0, a1` must copy a register, not
            // add a register number as an immediate.
            out.id = InstrId::ADD;
            out.rs1 = kZero;
            break;
        case InstrId::C_BEQZ:
            out.id = InstrId::BEQ;
            out.rs2 = kZero;
            break;
        case InstrId::C_BNEZ:
            out.id = InstrId::BNE;
            out.rs2 = kZero;
            break;

        // ---- control transfer, with an implicit link register -------------
        case InstrId::C_J:
            out.id = InstrId::JAL;
            out.rd = kZero;
            break;
        case InstrId::C_JAL:
            out.id = InstrId::JAL;
            out.rd = kRa;
            break;
        case InstrId::C_JR:
            // jalr x0, rs1, 0 -- the offset is always zero, which is why c.jr
            // cannot express `jalr rd, rs1, offset` and code that needs one
            // must use the 32-bit form.
            out.id = InstrId::JALR;
            out.rd = kZero;
            out.imm = 0;
            break;
        case InstrId::C_JALR:
            out.id = InstrId::JALR;
            out.rd = kRa;
            out.imm = 0;
            break;

        case InstrId::C_EBREAK:
            out.id = InstrId::EBREAK;
            break;

        // Every base instruction, and anything not listed above, stands for
        // itself. The guard at the top of the function has already returned for
        // those, so reaching here means a compressed row exists that nothing
        // above expands -- which unit_expand.cpp checks for by walking the
        // table, since a switch cannot be made exhaustive over the C_* subrange
        // alone.
        default:
            break;
    }

    return out;
}

}  // namespace rv::isa
