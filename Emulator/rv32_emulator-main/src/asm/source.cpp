#include "asm/source.hpp"

#include <algorithm>

namespace rv::as {

SourceSpan merge(SourceSpan first, SourceSpan second) {
    if (!first.valid()) return second;
    if (!second.valid()) return first;
    if (first.line != second.line) return first;
    const u16 begin = std::min(first.col, second.col);
    const u16 end = std::max(first.end_col(), second.end_col());
    return SourceSpan{first.line, begin, static_cast<u16>(end - begin)};
}

SourceFile::SourceFile(std::string name, std::string text)
    : name_(std::move(name)), text_(std::move(text)) {
    line_starts_.push_back(0);
    for (std::size_t i = 0; i < text_.size(); ++i) {
        if (text_[i] == '\n') line_starts_.push_back(i + 1);
    }
    // A trailing newline does not begin a further line.
    if (line_starts_.size() > 1 && line_starts_.back() == text_.size()) line_starts_.pop_back();
}

std::string_view SourceFile::line(u32 line_number) const {
    if (line_number == 0 || line_number > line_starts_.size()) return {};
    const std::size_t begin = line_starts_[line_number - 1];
    // Find the terminator rather than deriving it from the next line's start:
    // that also handles a final line with no trailing newline, and avoids the
    // off-by-one where the last line keeps its own '\n'.
    std::size_t end = text_.find('\n', begin);
    if (end == std::string::npos) end = text_.size();

    std::string_view view(text_);
    view = view.substr(begin, end - begin);
    // Strip a stray carriage return from CRLF files.
    if (!view.empty() && view.back() == '\r') view.remove_suffix(1);
    return view;
}

std::string_view SourceFile::snippet(SourceSpan span) const {
    const std::string_view source_line = line(span.line);
    if (span.col >= source_line.size()) return {};
    return source_line.substr(span.col, std::min<std::size_t>(span.len, source_line.size() - span.col));
}

}  // namespace rv::as
