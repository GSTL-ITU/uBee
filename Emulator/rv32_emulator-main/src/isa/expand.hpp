// Rewriting a compressed instruction as the base instruction it stands for.
//
// Every instruction in the C extension is defined by the spec as an alias: a
// shorter spelling of something RV32I could already say. `c.addi a0, 1` *is*
// `addi a0, a0, 1`, not a new operation that happens to resemble one.
//
// Taking that literally is what keeps the executor free of the C extension.
// src/core/exec.cpp has no compressed cases and never sees a C_* id; the hart
// expands first and runs the result, so all 27 compressed instructions inherit
// semantics that were already tested rather than getting a second
// implementation that can drift from the first.
#pragma once

#include "isa/decode.hpp"

namespace rv::isa {

/// The base instruction `instr` stands for, with its operands filled in.
///
/// `length` is carried across unchanged, so the caller still knows the pc
/// advances by 2 -- that is the one way the expansion is not equivalent to
/// having written the base instruction out longhand. `raw` is left as the
/// original halfword, because that is what belongs in mtval if it traps.
///
/// A non-compressed or invalid instruction is returned unchanged, so callers
/// that do not care can expand unconditionally.
DecodedInstr expand(const DecodedInstr& instr);

}  // namespace rv::isa
