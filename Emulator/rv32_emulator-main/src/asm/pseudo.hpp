#pragma once

#include <array>
#include <cstddef>
#include <string_view>

#include "isa/types.hpp"

namespace rv::as {

/// The operand shape a pseudo-instruction is written with. Distinct from the
/// real instruction syntaxes because a pseudo's operands do not map one to one
/// onto the encoding -- that is the whole point of it being a pseudo.
enum class PseudoShape : u8 {
    NONE,       // nop, ret
    RD_RS,      // mv rd, rs
    RS_LBL,     // beqz rs, label
    RS_RS_LBL,  // bgt rs, rt, label
    LBL,        // j label
    RS,         // jr rs
    RD_IMM,     // li rd, imm
    RD_SYM,     // la rd, symbol
    RD_CSR,     // csrr rd, csr
    CSR_RS,     // csrw csr, rs
    CSR_IMM,    // csrwi csr, imm
};

enum class PseudoId : u8 {
#define RV_PSEUDO(id, ...) id,
#include "asm/pseudo_table.def"
#undef RV_PSEUDO
    Count
};

inline constexpr std::size_t kPseudoCount = static_cast<std::size_t>(PseudoId::Count);
inline constexpr PseudoId kInvalidPseudo = PseudoId::Count;

struct PseudoDesc {
    std::string_view spelling;
    u8 operand_count;
    PseudoShape shape;
    /// Words emitted. LI overrides this once its literal is known.
    u8 words;
};

inline constexpr std::array<PseudoDesc, kPseudoCount> kPseudoTable = {{
#define RV_PSEUDO(id, spelling, operands, shape, words) \
    PseudoDesc{spelling, (operands), PseudoShape::shape, (words)},
#include "asm/pseudo_table.def"
#undef RV_PSEUDO
}};

const PseudoDesc& describe_pseudo(PseudoId id);

/// Look up by spelling *and* operand count, because a few names exist in both
/// forms: `jal label` is a pseudo, `jal rd, label` is the real instruction.
PseudoId lookup_pseudo(std::string_view spelling, std::size_t operand_count);

/// True if any pseudo uses this spelling, regardless of arity. Lets the
/// assembler say "jal takes 1 or 2 operands" rather than "unknown instruction".
bool is_pseudo_spelling(std::string_view spelling);

/// How many words `li rd, value` needs: one addi when the value fits in a
/// 12-bit signed immediate, otherwise lui + addi.
constexpr u8 li_word_count(i64 value) { return fits_signed(value, 12) ? 1 : 2; }

}  // namespace rv::as
