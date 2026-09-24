// Turning a decoded instruction back into text.
//
// Driven entirely by InstrDesc::syntax, so there is one small formatter per
// operand shape rather than one per instruction. Adding an instruction to
// instr_table.def makes it disassemble with no changes here.
#pragma once

#include <string>

#include "isa/decode.hpp"

namespace rv::isa {

struct DisasmOptions {
    /// Print `a0` rather than `x10`. Both spellings name the same register;
    /// the UI shows both side by side, but a single line of text has to pick.
    bool abi_names = true;
    /// Resolve pc-relative branch and jump targets to absolute addresses.
    /// Requires `pc` to be the address of this instruction.
    bool absolute_targets = true;
};

std::string disassemble(const DecodedInstr& instr, Addr pc, const DisasmOptions& options = {});

/// Convenience: decode and disassemble in one call.
std::string disassemble_word(Word word, Addr pc, const DisasmOptions& options = {});

}  // namespace rv::isa
