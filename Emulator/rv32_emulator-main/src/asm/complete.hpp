// Completion and operand hints for the editor.
//
// Both are derived from instr_table.def rather than written out by hand: the
// table already records every instruction's operand shape, so the signature
// shown for `addi` is generated from the same row the assembler encodes it
// with. Adding an instruction makes it completable and documented for free,
// and the hint cannot drift from what the assembler will accept.
//
// It lives here rather than in the interface so that it can be tested without
// one, and so a second front-end would get the same answers.
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "asm/symtab.hpp"
#include "isa/types.hpp"

namespace rv::as {

enum class CompletionKind : u8 {
    Instruction,
    Pseudo,
    Directive,
    Register,
    Csr,
    Symbol,
};

struct Completion {
    /// What to insert.
    std::string text;
    /// How it is written, e.g. "addi rd, rs1, imm".
    std::string signature;
    /// One short line of explanation.
    std::string detail;
    CompletionKind kind = CompletionKind::Instruction;
};

/// One operand's shape, for the hint shown while an instruction is being
/// written.
struct OperandHint {
    /// The name the specification uses: rd, rs1, imm, offset, csr.
    std::string name;
    /// A short form of what may go there: "reg(x0-x31)", "imm(-2048..2047)",
    /// "mem(offset(base))".
    std::string form;
};

/// The operand shapes of an instruction or pseudo-instruction, by mnemonic.
/// Empty if the name is not one.
std::vector<OperandHint> operand_hints(std::string_view mnemonic);

/// "addi  rd, rs1, imm" -- the whole signature on one line.
std::string signature_of(std::string_view mnemonic);

/// What is being written at `column` on this line.
struct Context {
    enum class Where : u8 {
        /// The leading position: an instruction, a pseudo, or a directive.
        Mnemonic,
        /// Inside an operand of a known mnemonic.
        Operand,
        /// Somewhere completion has nothing useful to say -- a comment, or a
        /// string.
        Nowhere,
    };

    Where where = Where::Mnemonic;
    /// The partial word under the caret, which is what gets matched.
    std::string prefix;
    /// Byte offset where `prefix` starts, so a caller can replace it.
    u16 prefix_begin = 0;
    /// The mnemonic this line is writing, when `where` is Operand.
    std::string mnemonic;
    /// Which operand the caret is in, counting from zero.
    std::size_t operand_index = 0;
    /// Byte offset where that operand's text begins -- just past the comma
    /// that opened it, or past the mnemonic for the first one. What lies
    /// between here and the caret is how much of the field is already written.
    u16 field_begin = 0;
};

Context context_at(std::string_view line, u16 column);

/// The operands still to be written, as the editor greys them in after the
/// caret: `addi a0, ` -> `rs1, imm`. Empty when there is nothing to fill in,
/// or when real text follows the caret and the ghost would collide with it.
///
/// The field being asked for shows its range when the name alone does not say
/// what fits -- `imm` becomes `imm(-2048..2047)`, while `rd` is already the
/// whole answer.
std::string ghost_text(std::string_view line, u16 column);

/// Candidates for the caret's position, best match first. `symbols` may be
/// null, in which case labels are not offered.
std::vector<Completion> complete(std::string_view line, u16 column,
                                 const SymbolTable* symbols = nullptr);

}  // namespace rv::as
