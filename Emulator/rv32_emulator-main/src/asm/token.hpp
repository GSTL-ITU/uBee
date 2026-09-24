#pragma once

#include <string_view>

#include "asm/directive.hpp"
#include "asm/source.hpp"
#include "isa/types.hpp"

namespace rv::as {

enum class TokenKind : u8 {
    End,        // no more tokens on this line
    Ident,      // a bare word: a symbol reference, a mnemonic, a CSR name
    Label,      // `loop:` -- the colon is consumed, `text` is "loop"
    Directive,  // `.text`, `.word` -- only spellings the table knows
    Number,
    StringLit,
    Register,  // x0..x31, ABI names, and `fp`
    Comma,
    LParen,
    RParen,
    Plus,
    Minus,
    Star,
    Slash,
    Percent,
    Amp,
    Pipe,
    Caret,
    Tilde,
    Shl,       // <<
    Shr,       // >>
    Dot,       // `.` on its own: the current address
    RelocHi,   // %hi
    RelocLo,   // %lo
    Comment,   // kept, because the syntax highlighter needs it
    Error,     // only produced in Tolerant mode; Strict mode diagnoses instead
};

const char* token_kind_name(TokenKind kind);

struct Token {
    TokenKind kind = TokenKind::End;
    SourceSpan span;
    /// The exact source text, borrowed from the line the lexer was given.
    std::string_view text;
    /// Value if `kind == Number`. Character literals produce a Number too.
    u64 number = 0;
    /// Register index if `kind == Register`.
    RegIdx reg = 0;
    /// Resolved directive if `kind == Directive`.
    DirectiveId directive = kInvalidDirective;
    /// Decoded contents if `kind == StringLit`, with escapes already applied.
    std::string value;

    bool is(TokenKind other) const { return kind == other; }
};

}  // namespace rv::as
