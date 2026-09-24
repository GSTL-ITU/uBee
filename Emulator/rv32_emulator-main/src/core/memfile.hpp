// Reading and writing Verilog $readmemh memory images.
//
// Format: one 32-bit word per line, lowercase %08x, no @ address directives.
// The byte address of line n is n * 4. See docs/memory-map.md.
#pragma once

#include <string>
#include <vector>

#include "isa/types.hpp"

namespace rv::core {

struct MemFileOptions {
    /// Emit the whole array rather than stopping at the last written word.
    /// Some $readmemh consumers warn when a memory is only partially filled.
    bool pad_to_full = false;
    /// Word count to pad to when `pad_to_full` is set.
    std::size_t full_size = 0;
    /// Append the disassembly of each word as a `// ...` comment. Useful for
    /// reading the file by hand; some toolchains dislike comments, so it is off
    /// by default.
    bool annotate = false;
};

struct MemFileResult {
    bool ok = false;
    std::string error;
};

/// Write `words` to `path`. Returns the error message rather than throwing, so
/// the CLI and the TUI can both report it their own way.
MemFileResult write_mem_file(const std::string& path, const std::vector<Word>& words,
                             const MemFileOptions& options = {});

/// Render to a string instead of a file, for tests and for the TUI's preview.
std::string render_mem_file(const std::vector<Word>& words, const MemFileOptions& options = {});

/// Read a $readmemh file. Tolerates blank lines, `//` and `#` comments, and
/// `@address` directives (which reposition the write cursor).
MemFileResult read_mem_file(const std::string& path, std::vector<Word>& out);

/// Parse from memory, for tests. `source_name` appears in error messages.
MemFileResult parse_mem_file(const std::string& text, const std::string& source_name,
                             std::vector<Word>& out);

/// Pack a little-endian byte image into words for export, rounding the length
/// up to a whole word.
std::vector<Word> pack_bytes_to_words(const std::vector<u8>& bytes);

}  // namespace rv::core
