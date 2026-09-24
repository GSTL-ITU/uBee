#include "asm/diag_render.hpp"

#include <algorithm>
#include <cstdio>
#include <sstream>

namespace rv::as {
namespace {

constexpr const char* kReset = "\033[0m";
constexpr const char* kBold = "\033[1m";
constexpr const char* kBlue = "\033[34m";

const char* severity_color(Severity severity) {
    switch (severity) {
        case Severity::Error: return "\033[31m";
        case Severity::Warning: return "\033[33m";
        case Severity::Note: return "\033[36m";
        case Severity::Help: return "\033[32m";
    }
    return "";
}

std::string paint(const std::string& text, const char* color, bool enabled) {
    if (!enabled || color[0] == '\0') return text;
    return std::string(color) + text + kReset;
}

/// Expand tabs so the caret lands under the right character. A tab in the
/// source is otherwise the classic reason underlines drift off target.
std::string expand_tabs(std::string_view text, u16& column) {
    std::string out;
    u16 adjusted = column;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '\t') {
            out.push_back(text[i]);
            continue;
        }
        const std::size_t width = 4 - (out.size() % 4);
        if (i < column) adjusted = static_cast<u16>(adjusted + width - 1);
        out.append(width, ' ');
    }
    column = adjusted;
    return out;
}

void render_snippet(std::ostringstream& out, const SourceFile& source, SourceSpan span,
                    const std::string& label, const char* color, const RenderOptions& options,
                    int gutter_width) {
    if (!span.valid()) return;

    u16 column = span.col;
    const std::string text = expand_tabs(source.line(span.line), column);
    const std::string bar = paint("|", kBlue, options.color);
    const std::string indent(static_cast<std::size_t>(gutter_width) + 1, ' ');

    char line_number[16];
    std::snprintf(line_number, sizeof line_number, "%*u", gutter_width, span.line);

    out << " " << paint(line_number, kBlue, options.color) << " " << bar << " " << text << "\n";
    out << indent << " " << bar << " ";
    out << std::string(column, ' ');
    // A zero-length span still needs one caret to point at something.
    out << paint(std::string(std::max<u16>(span.len, 1), '^'), color, options.color);
    if (!label.empty()) out << " " << paint(label, color, options.color);
    out << "\n";
}

}  // namespace

std::string render(const Diagnostic& diagnostic, const SourceFile& source,
                   const RenderOptions& options) {
    const char* color = severity_color(diagnostic.severity);
    std::ostringstream out;

    // Header: "error[E0102]: message"
    std::string heading = severity_name(diagnostic.severity);
    if (!diagnostic.code.empty()) heading += "[" + diagnostic.code + "]";
    out << paint(heading, color, options.color) << ": "
        << paint(diagnostic.message, kBold, options.color) << "\n";

    const int gutter_width =
        static_cast<int>(std::to_string(std::max<u32>(diagnostic.primary.line, 1)).size());
    // The familiar rustc/clang shape: the arrow sits one column left of the
    // vertical bar, and the bar lines up with the space after the line number.
    const std::string indent(static_cast<std::size_t>(gutter_width) + 1, ' ');

    if (diagnostic.primary.valid()) {
        char location[512];
        std::snprintf(location, sizeof location, "%s:%u:%u", source.name().c_str(),
                      diagnostic.primary.line, diagnostic.primary.col + 1);
        out << indent << paint("-->", kBlue, options.color) << " " << location << "\n";
        out << indent << " " << paint("|", kBlue, options.color) << "\n";
        render_snippet(out, source, diagnostic.primary, diagnostic.primary_label, color, options,
                       gutter_width);
    }

    for (const Label& label : diagnostic.labels) {
        render_snippet(out, source, label.span, label.text, kBlue, options, gutter_width);
    }

    if (!diagnostic.hint.empty()) {
        out << indent << " " << paint("|", kBlue, options.color) << "\n";
        out << paint("help", severity_color(Severity::Help), options.color) << ": "
            << diagnostic.hint << "\n";
    }

    return out.str();
}

std::string render_all(const DiagBag& diagnostics, const SourceFile& source,
                       const RenderOptions& options) {
    std::ostringstream out;
    for (const Diagnostic& diagnostic : diagnostics.items()) {
        out << render(diagnostic, source, options) << "\n";
    }

    if (diagnostics.error_count() > 0 || diagnostics.warning_count() > 0) {
        out << (diagnostics.error_count() == 1 ? "1 error" : std::to_string(diagnostics.error_count()) + " errors");
        if (diagnostics.warning_count() > 0) {
            out << ", "
                << (diagnostics.warning_count() == 1
                        ? "1 warning"
                        : std::to_string(diagnostics.warning_count()) + " warnings");
        }
        out << "\n";
    }
    return out.str();
}

std::size_t edit_distance(std::string_view a, std::string_view b, std::size_t limit) {
    if (a.size() > b.size()) std::swap(a, b);
    if (b.size() - a.size() > limit) return limit + 1;

    // Optimal string alignment: Levenshtein plus adjacent transposition, which
    // costs one extra row and makes `bqe` -> `beq` and `dnoe` -> `done` cost 1
    // instead of 2. Transposed letters are one of the two typos people actually
    // make, so without this the suggester stays silent exactly when it would
    // help most.
    std::vector<std::size_t> before_previous(a.size() + 1, 0);
    std::vector<std::size_t> previous(a.size() + 1);
    std::vector<std::size_t> current(a.size() + 1);
    for (std::size_t i = 0; i <= a.size(); ++i) previous[i] = i;

    for (std::size_t j = 1; j <= b.size(); ++j) {
        current[0] = j;
        std::size_t row_best = current[0];
        for (std::size_t i = 1; i <= a.size(); ++i) {
            const std::size_t substitution = previous[i - 1] + (a[i - 1] == b[j - 1] ? 0 : 1);
            current[i] = std::min({previous[i] + 1, current[i - 1] + 1, substitution});
            if (i > 1 && j > 1 && a[i - 1] == b[j - 2] && a[i - 2] == b[j - 1]) {
                current[i] = std::min(current[i], before_previous[i - 2] + 1);
            }
            row_best = std::min(row_best, current[i]);
        }
        if (row_best > limit) return limit + 1;  // no path can recover
        before_previous.swap(previous);
        previous.swap(current);
    }
    return previous[a.size()];
}

std::string_view closest_match(std::string_view word,
                               const std::vector<std::string_view>& candidates) {
    // Allow one edit for short words and two for longer ones. Being greedier
    // than that produces confident nonsense, which is worse than no suggestion.
    const std::size_t limit = word.size() <= 4 ? 1 : 2;

    std::string_view best;
    std::size_t best_distance = limit + 1;
    for (const std::string_view candidate : candidates) {
        const std::size_t distance = edit_distance(word, candidate, limit);
        if (distance < best_distance) {
            best_distance = distance;
            best = candidate;
        }
    }
    return best_distance <= limit ? best : std::string_view{};
}

}  // namespace rv::as
