#include "asm/diagnostic.hpp"

namespace rv::as {

const char* severity_name(Severity severity) {
    switch (severity) {
        case Severity::Error: return "error";
        case Severity::Warning: return "warning";
        case Severity::Note: return "note";
        case Severity::Help: return "help";
    }
    return "message";
}

void DiagBag::add(Diagnostic diagnostic) {
    if (diagnostic.severity == Severity::Error) ++error_count_;
    if (diagnostic.severity == Severity::Warning) ++warning_count_;
    items_.push_back(std::move(diagnostic));
}

void DiagBag::error(std::string code, std::string message, SourceSpan span,
                    std::string primary_label, std::string hint) {
    Diagnostic diagnostic;
    diagnostic.severity = Severity::Error;
    diagnostic.code = std::move(code);
    diagnostic.message = std::move(message);
    diagnostic.primary = span;
    diagnostic.primary_label = std::move(primary_label);
    diagnostic.hint = std::move(hint);
    add(std::move(diagnostic));
}

void DiagBag::warning(std::string code, std::string message, SourceSpan span,
                      std::string primary_label, std::string hint) {
    Diagnostic diagnostic;
    diagnostic.severity = Severity::Warning;
    diagnostic.code = std::move(code);
    diagnostic.message = std::move(message);
    diagnostic.primary = span;
    diagnostic.primary_label = std::move(primary_label);
    diagnostic.hint = std::move(hint);
    add(std::move(diagnostic));
}

void DiagBag::note(std::string message, SourceSpan span, std::string primary_label) {
    Diagnostic diagnostic;
    diagnostic.severity = Severity::Note;
    diagnostic.message = std::move(message);
    diagnostic.primary = span;
    diagnostic.primary_label = std::move(primary_label);
    add(std::move(diagnostic));
}

std::vector<const Diagnostic*> DiagBag::for_line(u32 line) const {
    std::vector<const Diagnostic*> found;
    for (const Diagnostic& diagnostic : items_) {
        if (diagnostic.primary.line == line) found.push_back(&diagnostic);
    }
    return found;
}

const Diagnostic* DiagBag::first_error() const {
    for (const Diagnostic& diagnostic : items_) {
        if (diagnostic.severity == Severity::Error) return &diagnostic;
    }
    return nullptr;
}

void DiagBag::clear() {
    items_.clear();
    error_count_ = 0;
    warning_count_ = 0;
}

}  // namespace rv::as
