#include "gui/highlighter.hpp"

#include "asm/lexer.hpp"
#include "asm/pseudo.hpp"
#include "isa/isa.hpp"

namespace rv::gui {
namespace {

QTextCharFormat make(const QColor& colour, bool bold = false, bool italic = false) {
    QTextCharFormat format;
    format.setForeground(colour);
    if (bold) format.setFontWeight(QFont::Bold);
    format.setFontItalic(italic);
    return format;
}

}  // namespace

Palette::Palette() {
    const auto set = [this](Role role, QTextCharFormat format) {
        formats_[static_cast<std::size_t>(role)] = std::move(format);
    };

    set(Role::Normal, make(QColor(0xd6, 0xd6, 0xd6)));
    set(Role::Mnemonic, make(QColor(0x6c, 0xb6, 0xff), true));
    set(Role::Register, make(QColor(0x5b, 0xd6, 0xc8)));
    set(Role::Number, make(QColor(0xe8, 0xa0, 0x4e)));
    set(Role::Label, make(QColor(0x89, 0xd1, 0x85), true));
    set(Role::Directive, make(QColor(0xc5, 0x92, 0xdd)));
    set(Role::Comment, make(QColor(0x7d, 0x85, 0x90), false, true));
    set(Role::String, make(QColor(0x89, 0xd1, 0x85)));
    set(Role::Punctuation, make(QColor(0xa8, 0xa8, 0xa8)));

    QTextCharFormat error = make(QColor(0xf2, 0x6d, 0x6d), true);
    error.setUnderlineStyle(QTextCharFormat::WaveUnderline);
    error.setUnderlineColor(QColor(0xf2, 0x6d, 0x6d));
    set(Role::Error, error);
}

const Palette& Palette::dark() {
    static const Palette palette;
    return palette;
}

Role role_for_token(const as::Token& token) {
    switch (token.kind) {
        case as::TokenKind::Comment: return Role::Comment;
        case as::TokenKind::Label: return Role::Label;
        case as::TokenKind::Directive: return Role::Directive;
        case as::TokenKind::Register: return Role::Register;
        case as::TokenKind::Number: return Role::Number;
        case as::TokenKind::StringLit: return Role::String;
        case as::TokenKind::Error: return Role::Error;

        case as::TokenKind::Ident:
            // Only a mnemonic if the tables say so, which is why `addii` stops
            // looking like an instruction the moment the second i is typed.
            if (isa::lookup_mnemonic(token.text) != isa::kInvalidInstr) return Role::Mnemonic;
            if (as::is_pseudo_spelling(token.text)) return Role::Mnemonic;
            return Role::Normal;

        case as::TokenKind::End: return Role::Normal;
        default: return Role::Punctuation;
    }
}

AsmHighlighter::AsmHighlighter(QTextDocument* document) : QSyntaxHighlighter(document) {}

void AsmHighlighter::highlightBlock(const QString& text) {
    // Qt hands us one block (line) at a time, and assembly has no multi-line
    // tokens, so the lexer can be run on it in isolation with no carried state.
    const QByteArray utf8 = text.toUtf8();
    const std::string_view view(utf8.constData(), static_cast<std::size_t>(utf8.size()));

    for (const as::Token& token : as::tokenize_for_highlight(view, 1)) {
        if (token.kind == as::TokenKind::End) break;
        const Role role = role_for_token(token);
        if (role == Role::Normal) continue;
        setFormat(token.span.col, token.span.len, Palette::dark().format(role));
    }
}

}  // namespace rv::gui
