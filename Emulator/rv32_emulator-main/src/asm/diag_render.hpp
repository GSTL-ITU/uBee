// Rendering diagnostics as plain text.
//
//   error[E0102]: immediate 5000 out of range for 'addi'
//     --> hello.s:12:18
//      |
//   12 |         addi a0, a1, 5000
//      |                      ^^^^ must be in -2048..2047 (12-bit signed)
//      |
//   help: load the constant first:  li t0, 5000   then  add a0, a1, t0
//
// The TUI draws the same Diagnostic data with colour instead of calling this,
// so the two never disagree about where the caret goes.
#pragma once

#include <string>

#include "asm/diagnostic.hpp"
#include "asm/source.hpp"

namespace rv::as {

struct RenderOptions {
    /// Wrap severity and caret in ANSI colour. Off when writing to a file.
    bool color = false;
};

std::string render(const Diagnostic& diagnostic, const SourceFile& source,
                   const RenderOptions& options = {});

/// Render every diagnostic in the bag, followed by a summary line.
std::string render_all(const DiagBag& diagnostics, const SourceFile& source,
                       const RenderOptions& options = {});

/// Suggest a replacement for `word` from `candidates`, using edit distance.
/// Returns an empty view when nothing is close enough to be worth guessing.
///
/// Thirty lines of code, and the highest value per line in the whole assembler:
/// `addii` -> `addi` and `lop` -> `loop` are the two mistakes every learner
/// makes, and turning them from a dead end into a fix is most of what makes an
/// error message feel helpful.
std::string_view closest_match(std::string_view word,
                               const std::vector<std::string_view>& candidates);

/// Levenshtein distance, capped at `limit` for early exit.
std::size_t edit_distance(std::string_view a, std::string_view b, std::size_t limit);

}  // namespace rv::as
