// The instruction table, generated three ways from instr_table.def.
#pragma once

#include <array>
#include <cstddef>
#include <string_view>

#include "types.hpp"

namespace rv::isa {

/// The six base formats, then the nine compressed ones. Order matters: every
/// format from CR onwards is 16 bits wide, which is what `InstrDesc::length()`
/// keys off, so new base formats go before CR and new compressed ones after.
enum class InstrFormat : u8 { R, I, S, B, U, J, CR, CI, CSS, CIW, CL, CS, CA, CB, CJ };

/// True for the 16-bit formats. See the ordering note above.
constexpr bool is_compressed_format(InstrFormat format) { return format >= InstrFormat::CR; }

/// How an instruction's operands are written in assembly and printed back out.
/// The assembler's parser and the disassembler's printer both switch over this,
/// which is why neither needs a per-instruction special case.
enum class OperandSyntax : u8 {
    NONE,          // ecall, ebreak, mret, wfi, fence
    RD_RS1_RS2,    // add   rd, rs1, rs2
    RD_RS1_IMM,    // addi  rd, rs1, imm
    RD_RS1_SHAMT,  // slli  rd, rs1, shamt
    RD_OFS_RS1,    // lw    rd, offset(rs1)   -- also jalr
    RS2_OFS_RS1,   // sw    rs2, offset(rs1)
    RS1_RS2_LBL,   // beq   rs1, rs2, label
    RD_UIMM,       // lui   rd, imm20
    RD_LBL,        // jal   rd, label
    RD_CSR_RS1,    // csrrw rd, csr, rs1
    RD_CSR_ZIMM,   // csrrwi rd, csr, zimm5

    // Compressed forms. A prime means the 3-bit register field, which reaches
    // x8-x15 only. These are split more finely than the base formats look like
    // they need -- C_RD_IMM and C_RD_SHAMT print identically -- because the
    // editor's operand hints are generated from this enum, and a hint that
    // offered -32..31 where the encoding accepts 0..31 would be a lie the
    // assembler then refuses.
    C_RD_IMM,          // c.addi   rd, imm
    C_RD_UIMM,         // c.lui    rd, imm
    C_RD_SHAMT,        // c.slli   rd, shamt
    C_RD_RS2,          // c.mv     rd, rs2
    C_RDP_IMM,         // c.andi   rd', imm
    C_RDP_UIMM,        // c.addi4spn rd', imm
    C_RDP_SHAMT,       // c.srli   rd', shamt
    C_RDP_RS2P,        // c.sub    rd', rs2'
    C_RS1,             // c.jr     rs1
    C_IMM,             // c.addi16sp imm
    C_LBL,             // c.j      label
    C_RS1P_LBL,        // c.beqz   rs1', label
    C_RDP_OFS_RS1P,    // c.lw     rd', offset(rs1')
    C_RS2P_OFS_RS1P,   // c.sw     rs2', offset(rs1')
    C_RD_OFS_SP,       // c.lwsp   rd, offset(sp)
    C_RS2_OFS_SP,      // c.swsp   rs2, offset(sp)
};

/// Coarse category, used for syntax highlighting colours and for grouping the
/// in-app instruction help.
enum class InstrGroup : u8 {
    ArithR,
    ArithI,
    Shift,
    Load,
    Store,
    Branch,
    Jump,
    Upper,
    MulDiv,
    Csr,
    System,
    Fence,
};

enum class InstrId : u16 {
#define RV_INSTR(id, ...) id,
#include "instr_table.def"
#undef RV_INSTR
    Count
};

inline constexpr std::size_t kInstrCount = static_cast<std::size_t>(InstrId::Count);

/// Sentinel meaning "not a valid instruction". Shares the value of `Count` so
/// that it can never collide with a real row.
inline constexpr InstrId kInvalidInstr = InstrId::Count;

struct InstrDesc {
    std::string_view mnemonic;
    InstrFormat format;
    Word match;
    Word mask;
    OperandSyntax syntax;
    InstrGroup group;

    /// Encoded width in bytes: 2 for the compressed formats, 4 otherwise.
    /// Derived rather than stored so that the 57 base rows did not all have to
    /// grow a column saying "4".
    constexpr u8 length() const { return is_compressed_format(format) ? 2 : 4; }
};

inline constexpr std::array<InstrDesc, kInstrCount> kInstrTable = {{
#define RV_INSTR(id, mnem, fmt, match, mask, syn, grp)                                    \
    InstrDesc{mnem, InstrFormat::fmt, (match), (mask), OperandSyntax::syn, InstrGroup::grp},
#include "instr_table.def"
#undef RV_INSTR
}};

/// Table row for a known-valid id. Passing kInvalidInstr is a programming
/// error; callers check validity first.
const InstrDesc& describe(InstrId id);

std::string_view mnemonic_of(InstrId id);

/// Look a mnemonic up by name, case-sensitive. Returns kInvalidInstr if the
/// name is not an instruction (it may still be a pseudo-instruction).
InstrId lookup_mnemonic(std::string_view name);

std::string_view format_name(InstrFormat format);
std::string_view group_name(InstrGroup group);

}  // namespace rv::isa
