// Syntax highlighting for the Qt editor.
//
// Like the terminal one, it runs the assembler's own lexer. Only one thing
// decides what a token is, so the colours here, the colours in the TUI, and
// what F5 will actually do can never disagree.
#pragma once

#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <array>

#include "asm/token.hpp"

namespace rv::gui {

/// Semantic roles. A view names a role, never a colour, so the palette is
/// one place and the highlighter says nothing about how things look.
enum class Role : int {
    Normal,
    Mnemonic,
    Register,
    Number,
    Label,
    Directive,
    Comment,
    String,
    Punctuation,
    Error,
    Count,
};

class Palette {
public:
    static const Palette& dark();
    const QTextCharFormat& format(Role role) const {
        return formats_[static_cast<std::size_t>(role)];
    }

private:
    Palette();
    std::array<QTextCharFormat, static_cast<std::size_t>(Role::Count)> formats_;
};

class AsmHighlighter final : public QSyntaxHighlighter {
    Q_OBJECT

public:
    explicit AsmHighlighter(QTextDocument* document);

protected:
    void highlightBlock(const QString& text) override;
};

Role role_for_token(const as::Token& token);

}  // namespace rv::gui
