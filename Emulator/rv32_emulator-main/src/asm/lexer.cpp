#include "asm/lexer.hpp"

#include <cctype>

#include "isa/regnames.hpp"

namespace rv::as {
namespace {

bool is_ident_start(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_' || c == '$' || c == '.';
}

bool is_ident_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' || c == '$' || c == '.';
}

int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

}  // namespace

const char* token_kind_name(TokenKind kind) {
    switch (kind) {
        case TokenKind::End: return "end of line";
        case TokenKind::Ident: return "identifier";
        case TokenKind::Label: return "label";
        case TokenKind::Directive: return "directive";
        case TokenKind::Number: return "number";
        case TokenKind::StringLit: return "string";
        case TokenKind::Register: return "register";
        case TokenKind::Comma: return "','";
        case TokenKind::LParen: return "'('";
        case TokenKind::RParen: return "')'";
        case TokenKind::Plus: return "'+'";
        case TokenKind::Minus: return "'-'";
        case TokenKind::Star: return "'*'";
        case TokenKind::Slash: return "'/'";
        case TokenKind::Percent: return "'%'";
        case TokenKind::Amp: return "'&'";
        case TokenKind::Pipe: return "'|'";
        case TokenKind::Caret: return "'^'";
        case TokenKind::Tilde: return "'~'";
        case TokenKind::Shl: return "'<<'";
        case TokenKind::Shr: return "'>>'";
        case TokenKind::Dot: return "'.'";
        case TokenKind::RelocHi: return "'%hi'";
        case TokenKind::RelocLo: return "'%lo'";
        case TokenKind::Comment: return "comment";
        case TokenKind::Error: return "invalid token";
    }
    return "token";
}

Lexer::Lexer(std::string_view text, u32 line, LexMode mode, DiagBag* diagnostics)
    : text_(text), line_(line), mode_(mode), diagnostics_(diagnostics) {}

Token Lexer::make(TokenKind kind, std::size_t start) {
    Token token;
    token.kind = kind;
    token.span = SourceSpan{line_, static_cast<u16>(start), static_cast<u16>(pos_ - start)};
    token.text = text_.substr(start, pos_ - start);
    return token;
}

Token Lexer::lex_error(std::size_t start, std::string code, std::string message) {
    Token token = make(TokenKind::Error, start);
    // Tolerant mode is for syntax highlighting, where a half-typed line is the
    // normal state of the world, not something to complain about.
    if (mode_ == LexMode::Strict && diagnostics_ != nullptr) {
        diagnostics_->error(std::move(code), std::move(message), token.span);
    }
    return token;
}

void Lexer::skip_whitespace() {
    while (pos_ < text_.size() && (text_[pos_] == ' ' || text_[pos_] == '\t')) ++pos_;
}

bool Lexer::read_escaped_char(u8& out) {
    if (pos_ >= text_.size()) return false;
    const char c = text_[pos_++];
    if (c != '\\') {
        out = static_cast<u8>(c);
        return true;
    }
    if (pos_ >= text_.size()) return false;
    const char escape = text_[pos_++];
    switch (escape) {
        case 'n': out = '\n'; return true;
        case 't': out = '\t'; return true;
        case 'r': out = '\r'; return true;
        case '0': out = '\0'; return true;
        case '\\': out = '\\'; return true;
        case '\'': out = '\''; return true;
        case '"': out = '"'; return true;
        case 'x': {
            int value = 0;
            int digits = 0;
            while (pos_ < text_.size() && digits < 2 && hex_digit(text_[pos_]) >= 0) {
                value = value * 16 + hex_digit(text_[pos_++]);
                ++digits;
            }
            if (digits == 0) return false;
            out = static_cast<u8>(value);
            return true;
        }
        default: return false;
    }
}

Token Lexer::lex_word(std::size_t start) {
    while (pos_ < text_.size() && is_ident_char(text_[pos_])) ++pos_;
    const std::string_view word = text_.substr(start, pos_ - start);

    // A trailing colon makes it a label definition; the colon is consumed so
    // that every consumer sees the bare name.
    if (pos_ < text_.size() && text_[pos_] == ':') {
        ++pos_;
        Token token = make(TokenKind::Label, start);
        token.text = word;
        return token;
    }

    // `.text` is a directive; `.L1` is a local label. Two things have to be
    // true: the table knows the spelling, and we are still in the leading
    // position. Without the second condition a local label named `.zero` or
    // `.align` would be unusable, and those are perfectly ordinary names.
    if (!past_leading_token_ && word.size() > 1 && word[0] == '.') {
        if (const DirectiveId id = lookup_directive(word); id != kInvalidDirective) {
            Token token = make(TokenKind::Directive, start);
            token.directive = id;
            return token;
        }
    }

    if (const auto reg = isa::parse_register(word)) {
        Token token = make(TokenKind::Register, start);
        token.reg = *reg;
        return token;
    }

    return make(TokenKind::Ident, start);
}

Token Lexer::lex_number(std::size_t start) {
    int base = 10;
    if (text_[pos_] == '0' && pos_ + 1 < text_.size()) {
        const char marker = text_[pos_ + 1];
        if (marker == 'x' || marker == 'X') {
            base = 16;
            pos_ += 2;
        } else if (marker == 'b' || marker == 'B') {
            base = 2;
            pos_ += 2;
        }
        // Deliberately no leading-zero octal: `.word 010` meaning 8 is a
        // footgun, and nothing in RISC-V assembly needs it.
    }

    u64 value = 0;
    std::size_t digits = 0;
    bool overflow = false;
    while (pos_ < text_.size()) {
        const char c = text_[pos_];
        if (c == '_') {  // digit separator, as in 0xdead_beef
            ++pos_;
            continue;
        }
        const int digit = hex_digit(c);
        if (digit < 0 || digit >= base) break;
        const u64 next = value * static_cast<u64>(base) + static_cast<u64>(digit);
        if (next < value) overflow = true;
        value = next;
        ++pos_;
        ++digits;
    }

    if (digits == 0) {
        // Something like `0x` with no digits, or a stray base marker.
        while (pos_ < text_.size() && is_ident_char(text_[pos_])) ++pos_;
        return lex_error(start, "E0002",
                         std::string("malformed numeric literal '") +
                             std::string(text_.substr(start, pos_ - start)) + "'");
    }
    // A number immediately followed by letters is a typo, not two tokens.
    if (pos_ < text_.size() && is_ident_char(text_[pos_]) && text_[pos_] != '.') {
        while (pos_ < text_.size() && is_ident_char(text_[pos_])) ++pos_;
        return lex_error(start, "E0002",
                         std::string("malformed numeric literal '") +
                             std::string(text_.substr(start, pos_ - start)) + "'");
    }

    Token token = make(TokenKind::Number, start);
    token.number = value;
    if (overflow) {
        if (mode_ == LexMode::Strict && diagnostics_ != nullptr) {
            diagnostics_->error("E0003", "numeric literal does not fit in 64 bits", token.span);
        }
    }
    return token;
}

Token Lexer::lex_char_literal(std::size_t start) {
    ++pos_;  // opening quote
    u8 value = 0;
    if (!read_escaped_char(value)) {
        return lex_error(start, "E0004", "unterminated or invalid character literal");
    }
    if (pos_ >= text_.size() || text_[pos_] != '\'') {
        while (pos_ < text_.size() && text_[pos_] != '\'') ++pos_;
        if (pos_ < text_.size()) ++pos_;
        return lex_error(start, "E0004",
                         "character literal must contain exactly one character");
    }
    ++pos_;  // closing quote

    Token token = make(TokenKind::Number, start);
    token.number = value;
    return token;
}

Token Lexer::lex_string(std::size_t start) {
    ++pos_;  // opening quote
    std::string value;
    while (pos_ < text_.size() && text_[pos_] != '"') {
        u8 character = 0;
        if (!read_escaped_char(character)) {
            return lex_error(start, "E0005", "invalid escape sequence in string literal");
        }
        value.push_back(static_cast<char>(character));
    }
    if (pos_ >= text_.size()) {
        return lex_error(start, "E0005", "unterminated string literal");
    }
    ++pos_;  // closing quote

    Token token = make(TokenKind::StringLit, start);
    token.value = std::move(value);
    return token;
}

Token Lexer::next() {
    Token token = scan();
    // Labels precede the statement they name, so they do not close the leading
    // position; comments end the line and never matter.
    if (token.kind != TokenKind::Label && token.kind != TokenKind::Comment &&
        token.kind != TokenKind::End) {
        past_leading_token_ = true;
    }
    return token;
}

Token Lexer::scan() {
    skip_whitespace();
    if (pos_ >= text_.size()) return make(TokenKind::End, pos_);

    const std::size_t start = pos_;
    const char c = text_[pos_];

    // Comments run to end of line. Both spellings are common in RISC-V
    // assembly, and `;` is accepted because students reach for it.
    if (c == '#' || c == ';') {
        pos_ = text_.size();
        return make(TokenKind::Comment, start);
    }
    if (c == '/' && pos_ + 1 < text_.size() && text_[pos_ + 1] == '/') {
        pos_ = text_.size();
        return make(TokenKind::Comment, start);
    }

    if (std::isdigit(static_cast<unsigned char>(c)) != 0) return lex_number(start);
    if (c == '\'') return lex_char_literal(start);
    if (c == '"') return lex_string(start);

    // `%hi(` and `%lo(` versus a bare `%` modulo operator.
    if (c == '%') {
        const std::string_view rest = text_.substr(pos_);
        if (rest.size() >= 3 && rest.compare(0, 3, "%hi") == 0) {
            pos_ += 3;
            return make(TokenKind::RelocHi, start);
        }
        if (rest.size() >= 3 && rest.compare(0, 3, "%lo") == 0) {
            pos_ += 3;
            return make(TokenKind::RelocLo, start);
        }
        ++pos_;
        return make(TokenKind::Percent, start);
    }

    // A lone `.` means the current address; `.text` is handled by lex_word.
    if (c == '.' && (pos_ + 1 >= text_.size() || !is_ident_char(text_[pos_ + 1]))) {
        ++pos_;
        return make(TokenKind::Dot, start);
    }
    if (is_ident_start(c)) return lex_word(start);

    switch (c) {
        case ',': ++pos_; return make(TokenKind::Comma, start);
        case '(': ++pos_; return make(TokenKind::LParen, start);
        case ')': ++pos_; return make(TokenKind::RParen, start);
        case '+': ++pos_; return make(TokenKind::Plus, start);
        case '-': ++pos_; return make(TokenKind::Minus, start);
        case '*': ++pos_; return make(TokenKind::Star, start);
        case '/': ++pos_; return make(TokenKind::Slash, start);
        case '&': ++pos_; return make(TokenKind::Amp, start);
        case '|': ++pos_; return make(TokenKind::Pipe, start);
        case '^': ++pos_; return make(TokenKind::Caret, start);
        case '~': ++pos_; return make(TokenKind::Tilde, start);
        case '<':
            if (pos_ + 1 < text_.size() && text_[pos_ + 1] == '<') {
                pos_ += 2;
                return make(TokenKind::Shl, start);
            }
            break;
        case '>':
            if (pos_ + 1 < text_.size() && text_[pos_ + 1] == '>') {
                pos_ += 2;
                return make(TokenKind::Shr, start);
            }
            break;
        default: break;
    }

    ++pos_;
    return lex_error(start, "E0001",
                     std::string("unexpected character '") + c + "'");
}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;
    while (true) {
        Token token = next();
        const bool done = token.kind == TokenKind::End;
        tokens.push_back(std::move(token));
        if (done) break;
    }
    return tokens;
}

std::vector<Token> tokenize_for_highlight(std::string_view text, u32 line) {
    return Lexer(text, line, LexMode::Tolerant).tokenize();
}

}  // namespace rv::as
