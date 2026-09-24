// Diagnostic rendering and the "did you mean" suggester.
//
// These are golden-text tests on purpose: the error messages are a feature of
// this tool, so a change in how they look should be a deliberate edit to an
// expectation rather than something that slips by unnoticed.
#include "asm/diag_render.hpp"
#include "rv_test.hpp"

using namespace rv;
using namespace rv::as;

RV_TEST(diag_render, points_the_caret_at_the_operand) {
    SourceFile source("hello.s",
                      ".text\n"
                      "_start:\n"
                      "        addi a0, a1, 5000\n");

    Diagnostic diagnostic;
    diagnostic.severity = Severity::Error;
    diagnostic.code = "E0102";
    diagnostic.message = "immediate 5000 out of range for 'addi'";
    diagnostic.primary = SourceSpan{3, 21, 4};
    diagnostic.primary_label = "must be in -2048..2047 (12-bit signed)";
    diagnostic.hint = "load the constant first:  li t0, 5000   then  add a0, a1, t0";

    RV_CHECK_STR(render(diagnostic, source),
                 "error[E0102]: immediate 5000 out of range for 'addi'\n"
                 "  --> hello.s:3:22\n"
                 "   |\n"
                 " 3 |         addi a0, a1, 5000\n"
                 "   |                      ^^^^ must be in -2048..2047 (12-bit signed)\n"
                 "   |\n"
                 "help: load the constant first:  li t0, 5000   then  add a0, a1, t0\n");
}

RV_TEST(diag_render, secondary_labels_add_context) {
    SourceFile source(
        "dup.s",
        "loop:\n"
        "    nop\n"
        "loop:\n");

    Diagnostic diagnostic;
    diagnostic.code = "E0201";
    diagnostic.message = "symbol 'loop' is defined twice";
    diagnostic.primary = SourceSpan{3, 0, 4};
    diagnostic.primary_label = "redefined here";
    diagnostic.labels.push_back(Label{SourceSpan{1, 0, 4}, "first defined here"});

    const std::string text = render(diagnostic, source);
    RV_CHECK_NE(text.find("redefined here"), std::string::npos);
    RV_CHECK_NE(text.find("first defined here"), std::string::npos);
}

RV_TEST(diag_render, a_zero_length_span_still_gets_one_caret) {
    SourceFile source("x.s", "    addi a0,\n");
    Diagnostic diagnostic;
    diagnostic.message = "expected a register";
    diagnostic.primary = SourceSpan::point(1, 12);
    const std::string text = render(diagnostic, source);
    RV_CHECK_NE(text.find("^"), std::string::npos);
}

RV_TEST(diag_render, tabs_do_not_shift_the_caret) {
    // A tab in the source is the classic reason an underline drifts off target.
    SourceFile source("t.s", "\taddi a0, a1, 5000\n");
    Diagnostic diagnostic;
    diagnostic.message = "out of range";
    diagnostic.primary = SourceSpan{1, 14, 4};  // the 5000, counting the tab as one byte

    const std::string text = render(diagnostic, source);
    // The rendered source line and the caret line must agree on where the
    // literal starts once the tab has been expanded.
    const std::size_t source_start = text.find("5000");
    const std::size_t caret_start = text.find("^^^^");
    RV_CHECK_NE(source_start, std::string::npos);
    RV_CHECK_NE(caret_start, std::string::npos);

    const std::size_t source_line_begin = text.rfind('\n', source_start) + 1;
    const std::size_t caret_line_begin = text.rfind('\n', caret_start) + 1;
    RV_CHECK_EQ(source_start - source_line_begin, caret_start - caret_line_begin);
}

RV_TEST(diag_render, summary_counts_errors_and_warnings) {
    SourceFile source("x.s", "nop\n");
    DiagBag bag;
    bag.error("E1", "first", SourceSpan{1, 0, 3});
    bag.error("E2", "second", SourceSpan{1, 0, 3});
    bag.warning("W1", "careful", SourceSpan{1, 0, 3});

    const std::string text = render_all(bag, source);
    RV_CHECK_NE(text.find("2 errors, 1 warning"), std::string::npos);
    RV_CHECK_EQ(bag.error_count(), std::size_t{2});
    RV_CHECK(!bag.ok());
}

RV_TEST(diag_render, for_line_finds_what_the_editor_gutter_needs) {
    DiagBag bag;
    bag.error("E1", "on line 3", SourceSpan{3, 0, 1});
    bag.warning("W1", "also line 3", SourceSpan{3, 4, 1});
    bag.error("E2", "on line 7", SourceSpan{7, 0, 1});

    RV_CHECK_EQ(bag.for_line(3).size(), std::size_t{2});
    RV_CHECK_EQ(bag.for_line(7).size(), std::size_t{1});
    RV_CHECK_EQ(bag.for_line(4).size(), std::size_t{0});
    RV_CHECK_STR(bag.first_error()->message, "on line 3");
}

RV_TEST(diag_render, edit_distance_basics) {
    RV_CHECK_EQ(edit_distance("addi", "addi", 4), std::size_t{0});
    RV_CHECK_EQ(edit_distance("addii", "addi", 4), std::size_t{1});
    RV_CHECK_EQ(edit_distance("lop", "loop", 4), std::size_t{1});
    RV_CHECK_EQ(edit_distance("", "abc", 4), std::size_t{3});
    // Early exit must report "further than the limit", not a wrong small value.
    RV_CHECK(edit_distance("abcdef", "zyxwvu", 2) > 2);
}

RV_TEST(diag_render, suggests_the_two_mistakes_everyone_makes) {
    const std::vector<std::string_view> mnemonics = {"addi", "add", "sub", "lw", "sw", "beq"};
    RV_CHECK_EQ(closest_match("addii", mnemonics), "addi");
    RV_CHECK_EQ(closest_match("ad", mnemonics), "add");
    RV_CHECK_EQ(closest_match("bqe", mnemonics), "beq");

    const std::vector<std::string_view> labels = {"loop", "done", "start"};
    RV_CHECK_EQ(closest_match("lop", labels), "loop");
    RV_CHECK_EQ(closest_match("dnoe", labels), "done");
}

RV_TEST(diag_render, declines_to_guess_when_nothing_is_close) {
    // Confident nonsense is worse than no suggestion at all.
    const std::vector<std::string_view> mnemonics = {"addi", "add", "sub", "lw"};
    RV_CHECK(closest_match("frobnicate", mnemonics).empty());
    RV_CHECK(closest_match("xyzzy", mnemonics).empty());
}
