// Tokenization, in both the strict and tolerant modes.
#include "asm/lexer.hpp"
#include "rv_test.hpp"

using namespace rv;
using namespace rv::as;

namespace {

std::vector<Token> lex(std::string_view text, DiagBag* diagnostics = nullptr) {
    return Lexer(text, 1, LexMode::Strict, diagnostics).tokenize();
}

/// Everything but the trailing End token.
std::vector<TokenKind> kinds(std::string_view text) {
    std::vector<TokenKind> out;
    for (const Token& token : lex(text)) {
        if (token.kind == TokenKind::End) break;
        out.push_back(token.kind);
    }
    return out;
}

}  // namespace

RV_TEST(lexer, a_typical_instruction_line) {
    const std::vector<Token> tokens = lex("    addi a0, a1, -12   # add twelve");
    // ident, reg, comma, reg, comma, minus, number, comment, end
    RV_CHECK_EQ(tokens.size(), std::size_t{9});
    RV_CHECK_EQ(static_cast<int>(tokens[0].kind), static_cast<int>(TokenKind::Ident));
    RV_CHECK_EQ(tokens[0].text, "addi");
    RV_CHECK_EQ(static_cast<int>(tokens[1].kind), static_cast<int>(TokenKind::Register));
    RV_CHECK_EQ(tokens[1].reg, 10);  // a0
    // A negative literal is a unary minus applied to a number, not a signed
    // token: the expression parser needs it that way for `-(x + 1)` to work.
    RV_CHECK_EQ(static_cast<int>(tokens[5].kind), static_cast<int>(TokenKind::Minus));
    RV_CHECK_EQ(static_cast<int>(tokens[6].kind), static_cast<int>(TokenKind::Number));
    RV_CHECK_EQ(tokens[6].number, 12u);
    RV_CHECK_EQ(static_cast<int>(tokens[7].kind), static_cast<int>(TokenKind::Comment));
}

RV_TEST(lexer, spans_point_at_the_right_columns) {
    // The editor underlines using these, so an off-by-one here shows up as a
    // caret under the wrong character.
    const std::vector<Token> tokens = lex("  addi a0");
    RV_CHECK_EQ(tokens[0].span.col, 2);
    RV_CHECK_EQ(tokens[0].span.len, 4);
    RV_CHECK_EQ(tokens[1].span.col, 7);
    RV_CHECK_EQ(tokens[1].span.len, 2);
}

RV_TEST(lexer, both_register_spellings_resolve_to_the_same_index) {
    RV_CHECK_EQ(lex("x10")[0].reg, 10);
    RV_CHECK_EQ(lex("a0")[0].reg, 10);
    RV_CHECK_EQ(lex("zero")[0].reg, 0);
    RV_CHECK_EQ(lex("x0")[0].reg, 0);
    RV_CHECK_EQ(lex("sp")[0].reg, 2);
    RV_CHECK_EQ(lex("fp")[0].reg, 8);  // alias for s0
    RV_CHECK_EQ(lex("s0")[0].reg, 8);
    RV_CHECK_EQ(lex("x31")[0].reg, 31);
}

RV_TEST(lexer, a_name_that_is_not_a_register_stays_an_identifier) {
    RV_CHECK_EQ(static_cast<int>(lex("a99")[0].kind), static_cast<int>(TokenKind::Ident));
    RV_CHECK_EQ(static_cast<int>(lex("x32")[0].kind), static_cast<int>(TokenKind::Ident));
    RV_CHECK_EQ(static_cast<int>(lex("loop")[0].kind), static_cast<int>(TokenKind::Ident));
}

RV_TEST(lexer, labels_consume_their_colon) {
    const std::vector<Token> tokens = lex("loop:  addi a0, a0, 1");
    RV_CHECK_EQ(static_cast<int>(tokens[0].kind), static_cast<int>(TokenKind::Label));
    RV_CHECK_EQ(tokens[0].text, "loop");
    RV_CHECK_EQ(static_cast<int>(tokens[1].kind), static_cast<int>(TokenKind::Ident));
}

RV_TEST(lexer, directives_are_distinguished_from_local_labels) {
    // `.text` is a directive the table knows. `.L1` is not, so it must stay an
    // identifier -- local labels beginning with a dot are entirely normal.
    RV_CHECK_EQ(static_cast<int>(lex(".text")[0].kind), static_cast<int>(TokenKind::Directive));
    RV_CHECK_EQ(static_cast<int>(lex(".word")[0].kind), static_cast<int>(TokenKind::Directive));
    RV_CHECK_EQ(static_cast<int>(lex(".L1")[0].kind), static_cast<int>(TokenKind::Ident));
    RV_CHECK_EQ(static_cast<int>(lex(".L1:")[0].kind), static_cast<int>(TokenKind::Label));
    // A lone dot is the current address.
    RV_CHECK_EQ(static_cast<int>(lex(".")[0].kind), static_cast<int>(TokenKind::Dot));
}

RV_TEST(lexer, a_directive_name_is_only_a_directive_in_the_leading_position) {
    // `.zero` and `.align` are directives *and* perfectly ordinary local label
    // names. Position decides, exactly as it does in a real assembler --
    // otherwise `beqz t0, .zero` would be unwritable.
    RV_CHECK_EQ(static_cast<int>(lex(".zero 4")[0].kind), static_cast<int>(TokenKind::Directive));

    const std::vector<Token> reference = lex("beqz t0, .zero");
    RV_CHECK_EQ(static_cast<int>(reference[3].kind), static_cast<int>(TokenKind::Ident));
    RV_CHECK_EQ(reference[3].text, ".zero");

    // A label still precedes the statement, so it does not close the position.
    const std::vector<Token> labelled = lex("here:  .align 2");
    RV_CHECK_EQ(static_cast<int>(labelled[0].kind), static_cast<int>(TokenKind::Label));
    RV_CHECK_EQ(static_cast<int>(labelled[1].kind), static_cast<int>(TokenKind::Directive));
}

RV_TEST(lexer, number_bases) {
    RV_CHECK_EQ(lex("42")[0].number, 42u);
    RV_CHECK_EQ(lex("0x2a")[0].number, 42u);
    RV_CHECK_EQ(lex("0X2A")[0].number, 42u);
    RV_CHECK_EQ(lex("0b101010")[0].number, 42u);
    RV_CHECK_EQ(lex("0xdead_beef")[0].number, 0xdeadbeefu);
    // Deliberately not octal: a leading zero does not change the base.
    RV_CHECK_EQ(lex("010")[0].number, 10u);
}

RV_TEST(lexer, character_literals_are_numbers) {
    RV_CHECK_EQ(lex("'A'")[0].number, 65u);
    RV_CHECK_EQ(lex("'\\n'")[0].number, 10u);
    RV_CHECK_EQ(lex("'\\0'")[0].number, 0u);
    RV_CHECK_EQ(lex("'\\x41'")[0].number, 65u);
    RV_CHECK_EQ(static_cast<int>(lex("'A'")[0].kind), static_cast<int>(TokenKind::Number));
}

RV_TEST(lexer, string_literals_decode_escapes) {
    const std::vector<Token> tokens = lex("\"hi\\n\"");
    RV_CHECK_EQ(static_cast<int>(tokens[0].kind), static_cast<int>(TokenKind::StringLit));
    RV_CHECK_STR(tokens[0].value, "hi\n");
}

RV_TEST(lexer, memory_operand_punctuation) {
    const std::vector<TokenKind> got = kinds("lw a0, 8(sp)");
    const std::vector<TokenKind> want = {TokenKind::Ident,  TokenKind::Register, TokenKind::Comma,
                                         TokenKind::Number, TokenKind::LParen,   TokenKind::Register,
                                         TokenKind::RParen};
    RV_CHECK_EQ(got.size(), want.size());
    for (std::size_t i = 0; i < std::min(got.size(), want.size()); ++i) {
        RV_CHECK_EQ(static_cast<int>(got[i]), static_cast<int>(want[i]));
    }
}

RV_TEST(lexer, relocation_operators_versus_modulo) {
    RV_CHECK_EQ(static_cast<int>(lex("%hi(msg)")[0].kind), static_cast<int>(TokenKind::RelocHi));
    RV_CHECK_EQ(static_cast<int>(lex("%lo(msg)")[0].kind), static_cast<int>(TokenKind::RelocLo));
    RV_CHECK_EQ(static_cast<int>(lex("7 % 3")[1].kind), static_cast<int>(TokenKind::Percent));
}

RV_TEST(lexer, shift_operators) {
    RV_CHECK_EQ(static_cast<int>(lex("1 << 4")[1].kind), static_cast<int>(TokenKind::Shl));
    RV_CHECK_EQ(static_cast<int>(lex("16 >> 2")[1].kind), static_cast<int>(TokenKind::Shr));
}

RV_TEST(lexer, all_three_comment_styles) {
    RV_CHECK_EQ(static_cast<int>(lex("# hash")[0].kind), static_cast<int>(TokenKind::Comment));
    RV_CHECK_EQ(static_cast<int>(lex("// slashes")[0].kind), static_cast<int>(TokenKind::Comment));
    RV_CHECK_EQ(static_cast<int>(lex("; semicolon")[0].kind), static_cast<int>(TokenKind::Comment));
}

RV_TEST(lexer, strict_mode_reports_bad_input) {
    DiagBag diagnostics;
    lex("addi a0, a1, 0xzz", &diagnostics);
    RV_CHECK(!diagnostics.ok());

    DiagBag more;
    lex("addi a0, a1, `", &more);
    RV_CHECK(!more.ok());
}

RV_TEST(lexer, tolerant_mode_never_diagnoses_and_always_makes_progress) {
    // The editor re-lexes every visible line on every keystroke, so a
    // half-typed line must produce tokens rather than complaints -- and must
    // not loop forever on input it cannot understand.
    const std::vector<std::string_view> half_typed = {
        "addi a0, a1, 0x", "lw a0, 8(", "\"unterminated", "'", "addi a0, `", "%", ".",
    };
    for (const std::string_view text : half_typed) {
        const std::vector<Token> tokens = tokenize_for_highlight(text, 1);
        RV_CHECK(!tokens.empty());
        RV_CHECK_EQ(static_cast<int>(tokens.back().kind), static_cast<int>(TokenKind::End));
    }
}

RV_TEST(lexer, tolerant_and_strict_agree_on_valid_input) {
    // The two modes must differ only in whether they complain. If they ever
    // disagreed about token boundaries, the editor would colour text one way
    // and the assembler would read it another.
    const std::vector<std::string_view> lines = {
        "loop:  addi a0, a0, -1    # count down",
        ".data",
        "msg: .asciz \"hello\"",
        "  beq a0, zero, done",
        "  csrrs t0, mstatus, zero",
    };
    for (const std::string_view line : lines) {
        const std::vector<Token> strict = Lexer(line, 1, LexMode::Strict).tokenize();
        const std::vector<Token> tolerant = tokenize_for_highlight(line, 1);
        RV_CHECK_EQ(strict.size(), tolerant.size());
        for (std::size_t i = 0; i < std::min(strict.size(), tolerant.size()); ++i) {
            RV_CHECK_EQ(static_cast<int>(strict[i].kind), static_cast<int>(tolerant[i].kind));
            RV_CHECK_EQ(strict[i].span.col, tolerant[i].span.col);
            RV_CHECK_EQ(strict[i].span.len, tolerant[i].span.len);
        }
    }
}
