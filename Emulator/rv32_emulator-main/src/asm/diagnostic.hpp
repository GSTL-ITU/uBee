// Assembler diagnostics.
//
// The error messages are a feature of this tool, not a side effect of it: a
// learner spends more time reading them than reading successful output. So a
// diagnostic carries everything needed to render a good one -- a stable code, a
// primary span to underline, secondary spans for context ("first defined
// here"), and an actionable hint -- and diag_render.cpp turns that into text
// while the TUI draws the same data with colour.
#pragma once

#include <string>
#include <vector>

#include "asm/source.hpp"

namespace rv::as {

enum class Severity : u8 { Error, Warning, Note, Help };

const char* severity_name(Severity severity);

/// A secondary underline with its own message, drawn beneath the primary one.
struct Label {
    SourceSpan span;
    std::string text;
};

struct Diagnostic {
    Severity severity = Severity::Error;
    /// Stable, greppable identifier such as "E0102". Stable means a message can
    /// be documented and searched for, and that a test can pin it without
    /// depending on the exact wording.
    std::string code;
    /// One line, lower case, no trailing period -- the same convention gcc,
    /// clang and rustc use.
    std::string message;
    SourceSpan primary;
    /// Short text printed at the end of the caret underline.
    std::string primary_label;
    std::vector<Label> labels;
    /// An actionable suggestion. Worth its weight: "did you mean 'loop'?" turns
    /// a dead end into a fix.
    std::string hint;
};

class DiagBag {
public:
    void add(Diagnostic diagnostic);

    void error(std::string code, std::string message, SourceSpan span,
               std::string primary_label = {}, std::string hint = {});
    void warning(std::string code, std::string message, SourceSpan span,
                 std::string primary_label = {}, std::string hint = {});
    void note(std::string message, SourceSpan span, std::string primary_label = {});

    bool ok() const { return error_count_ == 0; }
    std::size_t error_count() const { return error_count_; }
    std::size_t warning_count() const { return warning_count_; }
    bool empty() const { return items_.empty(); }

    const std::vector<Diagnostic>& items() const { return items_; }

    /// Everything reported on one source line, for the editor's gutter marker
    /// and inline underline. Called on every redraw for every visible line, so
    /// it stays a simple scan over a small vector rather than an index that has
    /// to be kept in sync.
    std::vector<const Diagnostic*> for_line(u32 line) const;

    /// The first error, for "jump to the problem" after a failed assemble.
    const Diagnostic* first_error() const;

    void clear();

private:
    std::vector<Diagnostic> items_;
    std::size_t error_count_ = 0;
    std::size_t warning_count_ = 0;
};

}  // namespace rv::as
