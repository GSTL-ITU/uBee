// The two-pass assembler.
//
// Pass 1 lays out addresses and collects symbols; pass 2 evaluates expressions
// and encodes. Sizes are final after pass 1 -- there is no branch relaxation,
// so `la` and `call` are always two words even when the target would have fit
// in one. That removes an entire category of fixed-point iteration, at the cost
// of one wasted word in a 4096-word memory. The assembler emits a note when it
// happens, which turns the wart into a lesson about what those two
// instructions are actually doing.
#pragma once

#include <string>
#include <vector>

#include "asm/diagnostic.hpp"
#include "asm/source.hpp"
#include "asm/sourcemap.hpp"
#include "asm/symtab.hpp"
#include "core/hart.hpp"
#include "isa/types.hpp"

namespace rv::as {

struct AssembleOptions {
    u32 imem_size = core::kDefaultImemSize;
    u32 dmem_size = core::kDefaultDmemSize;
    /// Warn on every sub-word load or store. A word-addressed Verilog memory
    /// with no byte enables cannot implement sb/sh, so this catches "works in
    /// the emulator, breaks on the FPGA" before synthesis.
    bool strict_word_mem = false;
    /// Report the `la`/`call` sizing note. On by default: it is a teaching
    /// message, and it explains disassembly that would otherwise look wrong.
    bool explain_pseudo_sizing = true;
};

struct AssembledProgram {
    std::vector<Word> imem_words;
    /// Bytes of code actually emitted, before the image is rounded up to a
    /// whole word. Not the same as `imem_words.size() * 4` once compressed
    /// instructions exist: a program ending on an odd halfword is padded by two
    /// bytes it did not ask for.
    std::size_t imem_bytes = 0;
    std::vector<u8> dmem_bytes;
    SymbolTable symbols;
    SourceMap map;
    DiagBag diagnostics;
    /// `_start` if the program defines one, otherwise 0.
    Addr entry = 0;

    bool ok() const { return diagnostics.ok(); }
};

AssembledProgram assemble(const SourceFile& source, const AssembleOptions& options = {});

/// Convenience for tests and for the editor's in-memory F5 path.
AssembledProgram assemble_text(std::string text, std::string name = "<input>",
                               const AssembleOptions& options = {});

}  // namespace rv::as
