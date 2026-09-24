// Source text and positions within it.
//
// Note the namespace: `asm` is a C++ keyword, so the assembler lives in
// `rv::as`. Deciding that before the first header rather than after forty
// files is why it is stated here.
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "isa/types.hpp"

namespace rv::as {

/// A range within one line. Lines are 1-based (what an editor shows), columns
/// are 0-based byte offsets within the line (what an editor indexes by).
/// Spans never cross a line break: assembly has no multi-line constructs, and
/// keeping that invariant makes the editor's gutter and underline trivial.
struct SourceSpan {
    u32 line = 0;
    u16 col = 0;
    u16 len = 0;

    bool valid() const { return line != 0; }
    u16 end_col() const { return static_cast<u16>(col + len); }

    /// A zero-length span at the start of `span`, for "expected something here".
    static SourceSpan point(u32 line, u16 col) { return SourceSpan{line, col, 0}; }
};

/// Combine two spans on the same line into one covering both.
SourceSpan merge(SourceSpan first, SourceSpan second);

class SourceFile {
public:
    SourceFile() = default;
    SourceFile(std::string name, std::string text);

    const std::string& name() const { return name_; }
    const std::string& text() const { return text_; }
    std::size_t line_count() const { return line_starts_.size(); }

    /// 1-based. Returns an empty view for out-of-range lines rather than
    /// faulting, so a renderer can point past the end of a truncated file.
    std::string_view line(u32 line_number) const;

    /// The text a span covers, clamped to the line.
    std::string_view snippet(SourceSpan span) const;

private:
    std::string name_;
    std::string text_;
    std::vector<std::size_t> line_starts_;
};

}  // namespace rv::as
