#include "asm/parser.hpp"

#include <string>

#include "asm/diag_render.hpp"
#include "asm/lexer.hpp"
#include "isa/csr.hpp"

namespace rv::as {
namespace {

/// Every name the assembler would accept as a mnemonic, for "did you mean".
const std::vector<std::string_view>& all_mnemonics() {
    static const std::vector<std::string_view> names = [] {
        std::vector<std::string_view> out;
        for (const isa::InstrDesc& desc : isa::kInstrTable) out.push_back(desc.mnemonic);
        for (const PseudoDesc& desc : kPseudoTable) out.push_back(desc.spelling);
        return out;
    }();
    return names;
}

class LineParser {
public:
    LineParser(std::vector<Token> tokens, u32 line, ExprArena& exprs, DiagBag& diagnostics)
        : tokens_(std::move(tokens)), line_(line), exprs_(exprs), diagnostics_(diagnostics) {}

    bool parse(AsmLine& out);

private:
    const Token& peek() const { return tokens_[pos_]; }
    const Token& advance() { return tokens_[pos_++]; }
    bool at(TokenKind kind) const { return tokens_[pos_].kind == kind; }
    bool at_end() const { return at(TokenKind::End) || at(TokenKind::Comment); }
    bool accept(TokenKind kind) {
        if (!at(kind)) return false;
        ++pos_;
        return true;
    }

    SourceSpan here() const { return peek().span; }

    void error(std::string code, std::string message, SourceSpan span,
               std::string label = {}, std::string hint = {}) {
        diagnostics_.error(std::move(code), std::move(message), span, std::move(label),
                           std::move(hint));
        failed_ = true;
    }

    bool parse_operands(AsmLine& out);
    bool parse_operand(Operand& out);
    bool resolve_mnemonic(AsmLine& out);

    // Expression grammar, lowest precedence first.
    int parse_expr() { return parse_or(); }
    int parse_or();
    int parse_and();
    int parse_shift();
    int parse_additive();
    int parse_multiplicative();
    int parse_unary();
    int parse_primary();

    std::vector<Token> tokens_;
    u32 line_;
    ExprArena& exprs_;
    DiagBag& diagnostics_;
    std::size_t pos_ = 0;
    bool failed_ = false;
};

int LineParser::parse_or() {
    int lhs = parse_and();
    while (lhs >= 0 && (at(TokenKind::Pipe) || at(TokenKind::Caret))) {
        const char op = at(TokenKind::Pipe) ? '|' : '^';
        const SourceSpan op_span = advance().span;
        const int rhs = parse_and();
        if (rhs < 0) return -1;
        Expr node;
        node.kind = Expr::Kind::Binary;
        node.op = op;
        node.lhs = lhs;
        node.rhs = rhs;
        node.span = merge(exprs_[lhs].span, merge(op_span, exprs_[rhs].span));
        lhs = exprs_.add(node);
    }
    return lhs;
}

int LineParser::parse_and() {
    int lhs = parse_shift();
    while (lhs >= 0 && at(TokenKind::Amp)) {
        const SourceSpan op_span = advance().span;
        const int rhs = parse_shift();
        if (rhs < 0) return -1;
        Expr node;
        node.kind = Expr::Kind::Binary;
        node.op = '&';
        node.lhs = lhs;
        node.rhs = rhs;
        node.span = merge(exprs_[lhs].span, merge(op_span, exprs_[rhs].span));
        lhs = exprs_.add(node);
    }
    return lhs;
}

int LineParser::parse_shift() {
    int lhs = parse_additive();
    while (lhs >= 0 && (at(TokenKind::Shl) || at(TokenKind::Shr))) {
        const char op = at(TokenKind::Shl) ? 'L' : 'R';
        const SourceSpan op_span = advance().span;
        const int rhs = parse_additive();
        if (rhs < 0) return -1;
        Expr node;
        node.kind = Expr::Kind::Binary;
        node.op = op;
        node.lhs = lhs;
        node.rhs = rhs;
        node.span = merge(exprs_[lhs].span, merge(op_span, exprs_[rhs].span));
        lhs = exprs_.add(node);
    }
    return lhs;
}

int LineParser::parse_additive() {
    int lhs = parse_multiplicative();
    while (lhs >= 0 && (at(TokenKind::Plus) || at(TokenKind::Minus))) {
        const char op = at(TokenKind::Plus) ? '+' : '-';
        const SourceSpan op_span = advance().span;
        const int rhs = parse_multiplicative();
        if (rhs < 0) return -1;
        Expr node;
        node.kind = Expr::Kind::Binary;
        node.op = op;
        node.lhs = lhs;
        node.rhs = rhs;
        node.span = merge(exprs_[lhs].span, merge(op_span, exprs_[rhs].span));
        lhs = exprs_.add(node);
    }
    return lhs;
}

int LineParser::parse_multiplicative() {
    int lhs = parse_unary();
    while (lhs >= 0 && (at(TokenKind::Star) || at(TokenKind::Slash) || at(TokenKind::Percent))) {
        const char op = at(TokenKind::Star) ? '*' : (at(TokenKind::Slash) ? '/' : '%');
        const SourceSpan op_span = advance().span;
        const int rhs = parse_unary();
        if (rhs < 0) return -1;
        Expr node;
        node.kind = Expr::Kind::Binary;
        node.op = op;
        node.lhs = lhs;
        node.rhs = rhs;
        node.span = merge(exprs_[lhs].span, merge(op_span, exprs_[rhs].span));
        lhs = exprs_.add(node);
    }
    return lhs;
}

int LineParser::parse_unary() {
    if (at(TokenKind::Minus) || at(TokenKind::Plus) || at(TokenKind::Tilde)) {
        const char op = at(TokenKind::Minus) ? '-' : (at(TokenKind::Plus) ? '+' : '~');
        const SourceSpan op_span = advance().span;
        const int operand = parse_unary();
        if (operand < 0) return -1;
        Expr node;
        node.kind = Expr::Kind::Unary;
        node.op = op;
        node.lhs = operand;
        node.span = merge(op_span, exprs_[operand].span);
        return exprs_.add(node);
    }
    return parse_primary();
}

int LineParser::parse_primary() {
    const Token& token = peek();

    switch (token.kind) {
        case TokenKind::Number: {
            Expr node;
            node.kind = Expr::Kind::Number;
            node.value = static_cast<i64>(token.number);
            node.span = token.span;
            advance();
            return exprs_.add(node);
        }
        case TokenKind::Ident: {
            Expr node;
            node.kind = Expr::Kind::Symbol;
            node.name = token.text;
            node.span = token.span;
            advance();
            return exprs_.add(node);
        }
        case TokenKind::Dot: {
            Expr node;
            node.kind = Expr::Kind::Dot;
            node.span = token.span;
            advance();
            return exprs_.add(node);
        }
        case TokenKind::LParen: {
            advance();
            const int inner = parse_expr();
            if (inner < 0) return -1;
            if (!accept(TokenKind::RParen)) {
                error("E0011", "expected ')'", here(), "unclosed parenthesis");
                return -1;
            }
            return inner;
        }
        case TokenKind::RelocHi:
        case TokenKind::RelocLo: {
            const bool is_hi = token.kind == TokenKind::RelocHi;
            const SourceSpan start = advance().span;
            if (!accept(TokenKind::LParen)) {
                error("E0012", std::string("expected '(' after '") + (is_hi ? "%hi" : "%lo") + "'",
                      here());
                return -1;
            }
            const int inner = parse_expr();
            if (inner < 0) return -1;
            if (!accept(TokenKind::RParen)) {
                error("E0011", "expected ')'", here(), "unclosed parenthesis");
                return -1;
            }
            Expr node;
            node.kind = is_hi ? Expr::Kind::HiReloc : Expr::Kind::LoReloc;
            node.lhs = inner;
            node.span = merge(start, exprs_[inner].span);
            return exprs_.add(node);
        }
        case TokenKind::Register:
            error("E0013", "a register cannot appear in an expression", token.span,
                  std::string(token.text) + " is a register",
                  "did you mean to write it as a separate operand, after a comma?");
            advance();
            return -1;
        default:
            error("E0010", std::string("expected a value, found ") + token_kind_name(token.kind),
                  token.span);
            return -1;
    }
}

bool LineParser::parse_operand(Operand& out) {
    out.span = here();

    if (at(TokenKind::Register)) {
        const Token& token = advance();
        out.kind = Operand::Kind::Register;
        out.reg = token.reg;
        out.span = token.span;
        return true;
    }

    if (at(TokenKind::StringLit)) {
        const Token& token = advance();
        out.kind = Operand::Kind::Text;
        out.text = token.value;
        out.span = token.span;
        return true;
    }

    // `8(sp)` and the bare `(sp)` shorthand for `0(sp)`.
    //
    // A leading '(' is ambiguous: `(sp)` is a memory operand with an implied
    // zero offset, while `(BASE + 2) * 4` is a parenthesised expression. Two
    // tokens of lookahead settle it -- only `( register )` is the shorthand.
    const bool bare_base = at(TokenKind::LParen) && pos_ + 2 < tokens_.size() &&
                           tokens_[pos_ + 1].kind == TokenKind::Register &&
                           tokens_[pos_ + 2].kind == TokenKind::RParen;

    int offset = -1;
    if (!bare_base) {
        offset = parse_expr();
        if (offset < 0) return false;
    }

    if (at(TokenKind::LParen)) {
        advance();
        if (!at(TokenKind::Register)) {
            error("E0014", "expected a base register inside the parentheses", here(),
                  "for example: 8(sp)");
            return false;
        }
        const Token& base = advance();
        if (!accept(TokenKind::RParen)) {
            error("E0011", "expected ')'", here(), "unclosed parenthesis");
            return false;
        }
        if (offset < 0) {
            Expr zero;
            zero.kind = Expr::Kind::Number;
            zero.value = 0;
            zero.span = base.span;
            offset = exprs_.add(zero);
        }
        out.kind = Operand::Kind::Memory;
        out.reg = base.reg;
        out.expr = offset;
        out.span = merge(out.span, base.span);
        return true;
    }

    out.kind = Operand::Kind::Expression;
    out.expr = offset;
    out.span = exprs_[offset].span;
    return true;
}

bool LineParser::parse_operands(AsmLine& out) {
    if (at_end()) return true;
    while (true) {
        Operand operand;
        if (!parse_operand(operand)) return false;
        out.operands.push_back(std::move(operand));
        if (!accept(TokenKind::Comma)) break;
        if (at_end()) {
            error("E0015", "expected another operand after ','", here());
            return false;
        }
    }
    if (!at_end()) {
        error("E0016",
              std::string("unexpected ") + token_kind_name(peek().kind) + " after the operands",
              here(), "", "operands are separated by commas");
        return false;
    }
    return true;
}

bool LineParser::resolve_mnemonic(AsmLine& out) {
    const std::size_t count = out.operands.size();

    // Pseudo-instructions are checked first, and by arity: `jal label` is the
    // pseudo, `jal rd, label` is the real instruction.
    if (const PseudoId pseudo = lookup_pseudo(out.mnemonic, count); pseudo != kInvalidPseudo) {
        out.kind = AsmLine::Kind::Pseudo;
        out.pseudo = pseudo;
        out.words = describe_pseudo(pseudo).words;
        return true;
    }

    if (const isa::InstrId instr = isa::lookup_mnemonic(out.mnemonic);
        instr != isa::kInvalidInstr) {
        out.kind = AsmLine::Kind::Instruction;
        out.instr = instr;
        out.words = 1;
        return true;
    }

    // The spelling exists but the operand count does not match any form.
    if (is_pseudo_spelling(out.mnemonic)) {
        std::string arities;
        for (const PseudoDesc& desc : kPseudoTable) {
            if (desc.spelling != out.mnemonic) continue;
            if (!arities.empty()) arities += " or ";
            arities += std::to_string(desc.operand_count);
        }
        error("E0021",
              std::string("'") + std::string(out.mnemonic) + "' does not take " +
                  std::to_string(count) + (count == 1 ? " operand" : " operands"),
              out.mnemonic_span, "wrong number of operands",
              std::string("it takes ") + arities);
        return false;
    }

    std::string hint;
    if (const std::string_view suggestion = closest_match(out.mnemonic, all_mnemonics());
        !suggestion.empty()) {
        hint = "did you mean '" + std::string(suggestion) + "'?";
    }
    error("E0020", std::string("unknown instruction '") + std::string(out.mnemonic) + "'",
          out.mnemonic_span, "not an instruction or pseudo-instruction", std::move(hint));
    return false;
}

bool LineParser::parse(AsmLine& out) {
    out.line = line_;

    while (at(TokenKind::Label)) {
        const Token& token = advance();
        out.labels.push_back(token.text);
        out.label_spans.push_back(token.span);
    }

    if (at_end()) {
        out.kind = AsmLine::Kind::Empty;
        return !failed_;
    }

    if (at(TokenKind::Directive)) {
        const Token& token = advance();
        out.kind = AsmLine::Kind::Directive;
        out.directive = token.directive;
        out.mnemonic = token.text;
        out.mnemonic_span = token.span;
        if (!parse_operands(out)) return false;
        return !failed_;
    }

    if (!at(TokenKind::Ident)) {
        error("E0017",
              std::string("expected an instruction, found ") + token_kind_name(peek().kind),
              here());
        return false;
    }

    const Token& mnemonic = advance();
    out.mnemonic = mnemonic.text;
    out.mnemonic_span = mnemonic.span;
    if (!parse_operands(out)) return false;
    if (!resolve_mnemonic(out)) return false;
    return !failed_;
}

}  // namespace

ParsedProgram parse(const SourceFile& source, DiagBag& diagnostics) {
    ParsedProgram program;
    const auto line_count = static_cast<u32>(source.line_count());

    for (u32 line = 1; line <= line_count; ++line) {
        std::vector<Token> tokens =
            Lexer(source.line(line), line, LexMode::Strict, &diagnostics).tokenize();

        AsmLine parsed;
        LineParser parser(std::move(tokens), line, program.exprs, diagnostics);
        if (!parser.parse(parsed)) {
            // Every line is recorded, failures included, so line numbers stay
            // aligned with the source. A line that looked like an instruction
            // is marked Error rather than Empty so that layout still reserves
            // its word.
            const bool looked_like_code =
                !parsed.mnemonic.empty() && parsed.kind != AsmLine::Kind::Directive;
            parsed.kind = looked_like_code ? AsmLine::Kind::Error : AsmLine::Kind::Empty;
        }
        program.lines.push_back(std::move(parsed));
    }

    return program;
}

}  // namespace rv::as
