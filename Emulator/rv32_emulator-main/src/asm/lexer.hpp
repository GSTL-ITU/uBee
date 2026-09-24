// The lexer, used for two jobs at once.
//
// Strict mode drives the assembler and reports diagnostics. Tolerant mode
// drives the editor's syntax highlighting: it never diagnoses and always
// produces a token, marking junk as TokenKind::Error.
//
// That they are the *same* lexer is the point. The colours in the editor and
// the assembler's understanding of the text cannot disagree, because there is
// only one thing deciding what a token is. "Green in the editor but a syntax
// error on F5" becomes structurally impossible.
#pragma once

#include <string_view>
#include <vector>

#include "asm/diagnostic.hpp"
#include "asm/token.hpp"

namespace rv::as {

enum class LexMode : u8 { Strict, Tolerant };

class Lexer {
public:
    /// Lexes one line. Assembly has no multi-line tokens, so a line can be
    /// re-lexed in isolation -- which is what lets the editor highlight only
    /// the visible rows without keeping any cached state.
    Lexer(std::string_view text, u32 line, LexMode mode = LexMode::Strict,
          DiagBag* diagnostics = nullptr);

    Token next();

    /// Every token on the line, ending with a single End token.
    std::vector<Token> tokenize();

private:
    Token scan();
    Token make(TokenKind kind, std::size_t start);
    void skip_whitespace();
    Token lex_word(std::size_t start);
    Token lex_number(std::size_t start);
    Token lex_char_literal(std::size_t start);
    Token lex_string(std::size_t start);
    Token lex_error(std::size_t start, std::string code, std::string message);
    /// Reads one character of a literal, applying backslash escapes.
    bool read_escaped_char(u8& out);

    std::string_view text_;
    u32 line_;
    LexMode mode_;
    DiagBag* diagnostics_;
    std::size_t pos_ = 0;
    /// True once something other than a label has been emitted on this line.
    /// A directive is only a directive in the leading position: `.zero` at the
    /// start of a statement reserves bytes, while `beqz t0, .zero` refers to a
    /// local label that happens to share the name.
    bool past_leading_token_ = false;
};

/// Convenience for the highlighter: tokenize without any diagnostics.
std::vector<Token> tokenize_for_highlight(std::string_view text, u32 line);

}  // namespace rv::as
