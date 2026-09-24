#include "isa/disasm.hpp"

#include <cstdio>

#include "isa/csr.hpp"
#include "isa/regnames.hpp"

namespace rv::isa {
namespace {

std::string reg_text(RegIdx reg, const DisasmOptions& options) {
    return std::string(options.abi_names ? abi_name(reg) : numeric_name(reg));
}

std::string hex_addr(Addr addr) {
    char buffer[16];
    std::snprintf(buffer, sizeof buffer, "0x%x", addr);
    return buffer;
}

/// CSRs print by name when we know one -- `csrr t0, mstatus` is much easier to
/// read than `csrr t0, 0x300`, and the name is what the assembler accepts back.
std::string csr_text(CsrAddr csr) {
    if (const CsrDesc* desc = find_csr_by_addr(csr)) return std::string(desc->name);
    char buffer[16];
    std::snprintf(buffer, sizeof buffer, "0x%x", csr);
    return buffer;
}

}  // namespace

std::string disassemble(const DecodedInstr& instr, Addr pc, const DisasmOptions& options) {
    if (!instr.valid()) {
        char buffer[32];
        // Render at the width the bits actually occupy: showing a bad halfword
        // as eight digits would invent four the fetch never looked at.
        if (instr.length == 2) {
            std::snprintf(buffer, sizeof buffer, ".half 0x%04x", instr.raw & 0xffffu);
        } else {
            std::snprintf(buffer, sizeof buffer, ".word 0x%08x", instr.raw);
        }
        return buffer;
    }

    const InstrDesc& desc = instr.desc();
    std::string text(desc.mnemonic);

    const auto rd = [&] { return reg_text(instr.rd, options); };
    const auto rs1 = [&] { return reg_text(instr.rs1, options); };
    const auto rs2 = [&] { return reg_text(instr.rs2, options); };
    const auto imm = [&] { return std::to_string(instr.imm); };
    const auto target = [&] {
        const Addr addr = pc + static_cast<u32>(instr.imm);
        return options.absolute_targets ? hex_addr(addr) : imm();
    };

    switch (desc.syntax) {
        case OperandSyntax::NONE:
            return text;
        case OperandSyntax::RD_RS1_RS2:
            return text + " " + rd() + ", " + rs1() + ", " + rs2();
        case OperandSyntax::RD_RS1_IMM:
        case OperandSyntax::RD_RS1_SHAMT:
            return text + " " + rd() + ", " + rs1() + ", " + imm();
        case OperandSyntax::RD_OFS_RS1:
            return text + " " + rd() + ", " + imm() + "(" + rs1() + ")";
        case OperandSyntax::RS2_OFS_RS1:
            return text + " " + rs2() + ", " + imm() + "(" + rs1() + ")";
        case OperandSyntax::RS1_RS2_LBL:
            return text + " " + rs1() + ", " + rs2() + ", " + target();
        case OperandSyntax::RD_UIMM: {
            // lui/auipc are written with the *upper* 20 bits as the operand,
            // not the byte value they produce.
            char buffer[16];
            std::snprintf(buffer, sizeof buffer, "0x%x", static_cast<u32>(instr.imm) >> 12);
            return text + " " + rd() + ", " + buffer;
        }
        case OperandSyntax::RD_LBL:
            return text + " " + rd() + ", " + target();
        case OperandSyntax::RD_CSR_RS1:
            return text + " " + rd() + ", " + csr_text(instr.csr) + ", " + rs1();
        case OperandSyntax::RD_CSR_ZIMM:
            return text + " " + rd() + ", " + csr_text(instr.csr) + ", " + imm();

        // ---- compressed ----------------------------------------------------
        // The operand *shapes* collapse to far fewer cases than the syntaxes,
        // because the syntaxes are split by what the encoding accepts rather
        // than by how it prints. c.addi and c.slli look the same here and
        // differ only in the range the assembler will take.
        case OperandSyntax::C_RD_IMM:
        case OperandSyntax::C_RD_SHAMT:
        case OperandSyntax::C_RDP_IMM:
        case OperandSyntax::C_RDP_UIMM:
        case OperandSyntax::C_RDP_SHAMT:
            return text + " " + rd() + ", " + imm();
        case OperandSyntax::C_RD_UIMM: {
            // c.lui, like lui, is written with the upper bits as the operand.
            char buffer[16];
            std::snprintf(buffer, sizeof buffer, "0x%x", (static_cast<u32>(instr.imm) >> 12) & 0x3fu);
            return text + " " + rd() + ", " + buffer;
        }
        case OperandSyntax::C_RD_RS2:
        case OperandSyntax::C_RDP_RS2P:
            return text + " " + rd() + ", " + rs2();
        case OperandSyntax::C_RS1:
            return text + " " + rs1();
        case OperandSyntax::C_IMM:
            return text + " " + imm();
        case OperandSyntax::C_LBL:
            return text + " " + target();
        case OperandSyntax::C_RS1P_LBL:
            return text + " " + rs1() + ", " + target();
        case OperandSyntax::C_RDP_OFS_RS1P:
        case OperandSyntax::C_RD_OFS_SP:
            return text + " " + rd() + ", " + imm() + "(" + rs1() + ")";
        case OperandSyntax::C_RS2P_OFS_RS1P:
        case OperandSyntax::C_RS2_OFS_SP:
            return text + " " + rs2() + ", " + imm() + "(" + rs1() + ")";
    }
    return text;
}

std::string disassemble_word(Word word, Addr pc, const DisasmOptions& options) {
    return disassemble(decode(word), pc, options);
}

}  // namespace rv::isa
