// The debugger engine: stepping, breakpoints and reverse execution.
#include "cmd/command.hpp"
#include "dbg/session.hpp"
#include "rv_test.hpp"

using namespace rv;
using namespace rv::dbg;

namespace {

DebugSession make_session(const std::string& text) {
    DebugSession session;
    as::AssembleOptions options;
    options.explain_pseudo_sizing = false;
    if (!session.load_source(text, "test.s", options)) {
        std::printf("  (program did not assemble)\n");
    }
    return session;
}

u32 reg(const DebugSession& session, RegIdx index) { return session.cpu().x[index]; }

}  // namespace

RV_TEST(session, step_line_covers_a_whole_pseudo_instruction) {
    // `li a0, 0x12345` is one source line and two instructions. Stepping by
    // line must execute both; stepping by instruction must show the seam.
    DebugSession session = make_session(
        "        li a0, 0x12345\n"   // line 1, two words
        "        li a1, 7\n"         // line 2, one word
        "        ebreak\n");

    RV_CHECK_EQ(*session.current_line(), 1u);
    session.step(StepKind::Line);
    RV_CHECK_EQ(*session.current_line(), 2u);
    RV_CHECK_HEX(reg(session, 10), 0x12345u);
}

RV_TEST(session, step_instruction_shows_the_two_halves_of_a_pseudo) {
    DebugSession session = make_session(
        "        li a0, 0x12345\n"
        "        ebreak\n");

    // First half: still on line 1, now at slot 1 of 2. This is the moment a
    // learner sees that one line became two instructions.
    session.step(StepKind::Instruction);
    RV_CHECK_EQ(*session.current_line(), 1u);
    const auto slot = session.current_slot();
    RV_CHECK(slot.has_value());
    RV_CHECK_EQ(slot->first, 1);
    RV_CHECK_EQ(slot->second, 2);
    RV_CHECK(!session.map().is_line_start(session.cpu().pc));

    session.step(StepKind::Instruction);
    RV_CHECK_EQ(*session.current_line(), 2u);
}

RV_TEST(session, step_line_never_stops_inside_a_pseudo) {
    DebugSession session = make_session(
        "        li a0, 0x11111\n"
        "        li a1, 0x22222\n"
        "        li a2, 0x33333\n"
        "        ebreak\n");
    for (int i = 0; i < 3; ++i) {
        session.step(StepKind::Line);
        // Every landing must be the first word of a line.
        RV_CHECK(session.map().is_line_start(session.cpu().pc));
    }
    RV_CHECK_HEX(reg(session, 12), 0x33333u);
}

RV_TEST(session, next_runs_a_call_to_completion_but_step_enters_it) {
    const std::string program =
        "_start: li a0, 1\n"
        "        call helper\n"
        "        li a2, 3\n"
        "        ebreak\n"
        "helper: li a1, 2\n"
        "        ret\n";

    DebugSession over = make_session(program);
    over.step(StepKind::Line);       // li a0, 1
    over.step(StepKind::LineOver);   // call, run to completion
    RV_CHECK_HEX(reg(over, 11), 2u);         // the helper did run
    RV_CHECK_EQ(*over.current_line(), 3u);   // and we are back on the next line

    DebugSession into = make_session(program);
    into.step(StepKind::Line);
    into.step(StepKind::Line);       // call, stepping in
    RV_CHECK_EQ(*into.current_line(), 5u);   // inside the helper
}

RV_TEST(session, finish_returns_from_the_current_function) {
    DebugSession session = make_session(
        "_start: call helper\n"
        "        li a2, 3\n"
        "        ebreak\n"
        "helper: li a1, 2\n"
        "        li a1, 9\n"
        "        ret\n");
    session.step(StepKind::Line);   // into helper
    RV_CHECK_EQ(*session.current_line(), 4u);
    session.step(StepKind::Out);
    RV_CHECK_HEX(reg(session, 11), 9u);      // the whole helper ran
    RV_CHECK_EQ(*session.current_line(), 2u);
}

RV_TEST(session, next_steps_over_a_recursive_call) {
    // The stack-depth guard exists for exactly this. Without it, `next` would
    // stop the first time control reached the return address -- which for a
    // recursive function is the innermost frame, not the outermost one.
    DebugSession session = make_session(
        "_start: li   sp, 0x1000\n"
        "        li   a0, 4\n"
        "        call fact\n"
        "        li   a2, 99\n"
        "        ebreak\n"
        "fact:   addi sp, sp, -8\n"
        "        sw   ra, 0(sp)\n"
        "        sw   a0, 4(sp)\n"
        "        li   t0, 1\n"
        "        ble  a0, t0, .base\n"
        "        addi a0, a0, -1\n"
        "        call fact\n"
        "        lw   t1, 4(sp)\n"
        "        mul  a0, a0, t1\n"
        ".base:  lw   ra, 0(sp)\n"
        "        addi sp, sp, 8\n"
        "        ret\n");

    session.step(StepKind::Line);      // li sp
    session.step(StepKind::Line);      // li a0
    RV_CHECK_EQ(*session.current_line(), 3u);

    session.step(StepKind::LineOver);  // over the whole recursion
    RV_CHECK_EQ(*session.current_line(), 4u);
    RV_CHECK_HEX(reg(session, 10), 24u);     // 4! computed
    RV_CHECK_HEX(reg(session, 2), 0x1000u);  // and the stack fully unwound
}

RV_TEST(session, finish_leaves_only_the_innermost_frame_of_a_recursion) {
    DebugSession session = make_session(
        "_start: li   sp, 0x1000\n"
        "        li   a0, 3\n"
        "        call fact\n"
        ""
        "        ebreak\n"
        "fact:   addi sp, sp, -8\n"
        "        sw   ra, 0(sp)\n"
        "        sw   a0, 4(sp)\n"
        "        li   t0, 1\n"
        "        ble  a0, t0, .base\n"
        "        addi a0, a0, -1\n"
        "        call fact\n"
        "        lw   t1, 4(sp)\n"
        "        mul  a0, a0, t1\n"
        ".base:  lw   ra, 0(sp)\n"
        "        addi sp, sp, 8\n"
        "        ret\n");

    // Stop in the base case, at the deepest frame.
    session.breakpoints().add_line(13, session.map());
    RV_CHECK_EQ(static_cast<int>(session.run().reason), static_cast<int>(StopReason::Breakpoint));
    const u32 deep_sp = reg(session, 2);
    RV_CHECK(deep_sp < 0x1000u);  // we really are nested

    session.step(StepKind::Out);
    // One frame's worth of stack came back, not all of it: `finish` leaves the
    // current call, not the whole recursion.
    RV_CHECK(reg(session, 2) > deep_sp);
    RV_CHECK(reg(session, 2) < 0x1000u);
    RV_CHECK(!session.halted());
}

RV_TEST(session, reverse_step_restores_state_exactly) {
    // Step forward N times, step back N times, and require the machine to be
    // bit-for-bit where it started. This is the strongest possible test of the
    // delta records, and it covers registers, memory and the counters at once.
    DebugSession session = make_session(
        "        li   a1, 0x40\n"
        "        li   a0, 5\n"
        "        sw   a0, 0(a1)\n"
        "        addi a0, a0, 1\n"
        "        sw   a0, 4(a1)\n"
        "        addi a0, a0, 1\n"
        "        ebreak\n");

    const core::CpuState before = session.cpu();
    const u32 mem_before_0 = session.read_dmem_word(0x40);
    const u32 mem_before_4 = session.read_dmem_word(0x44);

    constexpr int kSteps = 6;
    for (int i = 0; i < kSteps; ++i) session.step(StepKind::Instruction);
    RV_CHECK_HEX(session.read_dmem_word(0x40), 5u);
    RV_CHECK_HEX(session.read_dmem_word(0x44), 6u);

    for (int i = 0; i < kSteps; ++i) session.reverse_step(StepKind::Instruction);

    RV_CHECK_HEX(session.cpu().pc, before.pc);
    for (RegIdx r = 0; r < kNumRegs; ++r) RV_CHECK_HEX(session.cpu().x[r], before.x[r]);
    RV_CHECK_EQ(session.cpu().cycle, before.cycle);
    RV_CHECK_EQ(session.cpu().instret, before.instret);
    RV_CHECK_HEX(session.read_dmem_word(0x40), mem_before_0);
    RV_CHECK_HEX(session.read_dmem_word(0x44), mem_before_4);
}

RV_TEST(session, reverse_step_undoes_a_sub_word_store_correctly) {
    // sb changes one byte of a word. Undo stores the whole pre-image word back,
    // which is why no byte-mask bookkeeping is needed.
    DebugSession session = make_session(
        "        li a1, 0x40\n"
        "        li a0, 0xaaaaaaaa\n"
        "        sw a0, 0(a1)\n"
        "        li a0, 0x11\n"
        "        sb a0, 0(a1)\n"
        "        ebreak\n");
    for (int i = 0; i < 6; ++i) session.step(StepKind::Instruction);
    RV_CHECK_HEX(session.read_dmem_word(0x40), 0xaaaaaa11u);

    session.reverse_step(StepKind::Instruction);  // undo the sb
    RV_CHECK_HEX(session.read_dmem_word(0x40), 0xaaaaaaaau);
}

RV_TEST(session, reverse_step_by_line_is_symmetric_with_forward) {
    DebugSession session = make_session(
        "        li a0, 0x11111\n"
        "        li a1, 0x22222\n"
        "        li a2, 3\n"
        "        ebreak\n");
    session.step(StepKind::Line);
    session.step(StepKind::Line);
    RV_CHECK_EQ(*session.current_line(), 3u);

    session.reverse_step(StepKind::Line);
    RV_CHECK_EQ(*session.current_line(), 2u);
    RV_CHECK_HEX(reg(session, 11), 0u);  // the second li was undone whole
    RV_CHECK_HEX(reg(session, 10), 0x11111u);
}

RV_TEST(session, reverse_step_undoes_uart_output) {
    DebugSession session = make_session(
        "        li a1, 0xffff0000\n"
        "        li a0, 'h'\n"
        "        sb a0, 0(a1)\n"
        "        ebreak\n");
    for (int i = 0; i < 4; ++i) session.step(StepKind::Instruction);
    RV_CHECK_STR(session.uart_output(), "h");

    // A byte already sent to real hardware could not be recalled, but a
    // buffered console can drop it again.
    session.reverse_step(StepKind::Instruction);
    session.reverse_step(StepKind::Instruction);
    RV_CHECK_STR(session.uart_output(), "");
}

RV_TEST(session, reverse_past_the_start_reports_the_horizon) {
    DebugSession session = make_session("li a0, 1\nebreak\n");
    session.step(StepKind::Instruction);
    RV_CHECK(session.can_reverse());
    session.reverse_step(StepKind::Instruction);
    RV_CHECK(!session.can_reverse());
    const StopEvent event = session.reverse_step(StepKind::Instruction);
    RV_CHECK_EQ(static_cast<int>(event.reason), static_cast<int>(StopReason::HistoryHorizon));
}

RV_TEST(session, reverse_step_unhalts_the_machine) {
    // Stepping back out of an ebreak has to make the machine runnable again,
    // or the debugger would be stuck at the end of every program.
    DebugSession session = make_session("li a0, 1\nebreak\n");
    session.run();
    RV_CHECK(session.halted());
    session.reverse_step(StepKind::Instruction);
    RV_CHECK(!session.halted());
}

RV_TEST(session, breakpoints_stop_execution) {
    DebugSession session = make_session(
        "        li a0, 1\n"
        "        li a1, 2\n"
        "        li a2, 3\n"
        "        ebreak\n");
    RV_CHECK(session.breakpoints().add_line(3, session.map()) != nullptr);

    const StopEvent event = session.run();
    RV_CHECK_EQ(static_cast<int>(event.reason), static_cast<int>(StopReason::Breakpoint));
    RV_CHECK_EQ(*session.current_line(), 3u);
    RV_CHECK_HEX(reg(session, 11), 2u);  // line 2 ran
    RV_CHECK_HEX(reg(session, 12), 0u);  // line 3 did not
}

RV_TEST(session, a_breakpoint_on_a_blank_line_snaps_forward_and_says_so) {
    DebugSession session = make_session(
        "        li a0, 1\n"
        "\n"
        "# just a comment\n"
        "        li a1, 2\n"
        "        ebreak\n");
    const Breakpoint* breakpoint = session.breakpoints().add_line(2, session.map());
    RV_CHECK(breakpoint != nullptr);
    RV_CHECK(breakpoint->snapped);
    RV_CHECK_EQ(breakpoint->requested_line, 2u);
    RV_CHECK_EQ(breakpoint->line, 4u);
}

RV_TEST(session, breakpoints_survive_a_reassemble) {
    // The whole point of keying breakpoints by line: the editor's F5 loop
    // reassembles constantly, and losing breakpoints each time would make the
    // tool unusable.
    DebugSession session = make_session(
        "        li a0, 1\n"
        "        li a1, 2\n"
        "        ebreak\n");
    session.breakpoints().add_line(2, session.map());

    // Edit an earlier line so that line 2's address moves, then reassemble.
    // The breakpoint is keyed by line, so it must follow line 2 to its new
    // address rather than being stranded at the old one.
    //
    // Note that it follows the line *number*, not the text: inserting a line
    // above would leave it on line 2, now a different instruction. Keeping the
    // marker attached to the text as it moves is the editor's job in M6, since
    // only the editor knows what was inserted or deleted.
    as::AssembleOptions options;
    options.explain_pseudo_sizing = false;
    RV_CHECK(session.load_source(
        "        li a0, 0x12345\n"  // now two words, so line 2 shifts by 4
        "        li a1, 2\n"
        "        ebreak\n",
        "test.s", options));

    RV_CHECK_EQ(session.breakpoints().breakpoints().size(), std::size_t{1});
    const Breakpoint& breakpoint = session.breakpoints().breakpoints().front();
    RV_CHECK_EQ(breakpoint.line, 2u);
    RV_CHECK_HEX(breakpoint.addr, 8u);  // was 4 before line 1 grew
    RV_CHECK(breakpoint.enabled);

    // And it still actually stops there.
    RV_CHECK_EQ(static_cast<int>(session.run().reason), static_cast<int>(StopReason::Breakpoint));
    RV_CHECK_EQ(*session.current_line(), 2u);
}

RV_TEST(session, a_breakpoint_whose_line_loses_its_code_is_disabled_not_dropped) {
    DebugSession session = make_session(
        "        li a0, 1\n"
        "        li a1, 2\n"
        "        ebreak\n");
    session.breakpoints().add_line(2, session.map());

    as::AssembleOptions options;
    options.explain_pseudo_sizing = false;
    RV_CHECK(session.load_source(
        "        li a0, 1\n"
        "# the instruction here was deleted\n"
        "        ebreak\n",
        "test.s", options));

    RV_CHECK_EQ(session.breakpoints().breakpoints().size(), std::size_t{1});
    RV_CHECK(!session.breakpoints().breakpoints().front().enabled);
}

RV_TEST(session, watchpoints_fire_when_a_word_changes) {
    DebugSession session = make_session(
        "        li a1, 0x40\n"
        "        li a0, 7\n"
        "        sw a0, 0(a1)\n"
        "        li a2, 1\n"
        "        ebreak\n");
    session.breakpoints().add_watch(0x40, session.read_dmem_word(0x40));

    const StopEvent event = session.run();
    RV_CHECK_EQ(static_cast<int>(event.reason), static_cast<int>(StopReason::Watchpoint));
    RV_CHECK_HEX(event.watch_new, 7u);
    RV_CHECK_HEX(reg(session, 12), 0u);  // stopped before the next line
}

RV_TEST(session, an_infinite_loop_hits_the_watchdog_rather_than_hanging) {
    SessionOptions options;
    options.watchdog = 5000;
    DebugSession session(options);
    as::AssembleOptions assemble_options;
    assemble_options.explain_pseudo_sizing = false;
    RV_CHECK(session.load_source("spin:   j spin\n", "test.s", assemble_options));

    const StopEvent event = session.run();
    RV_CHECK_EQ(static_cast<int>(event.reason), static_cast<int>(StopReason::Watchdog));
}

RV_TEST(session, reset_keeps_breakpoints_and_reloads_data) {
    DebugSession session = make_session(
        "        .data\n"
        "slot:   .word 0x1234\n"
        "        .text\n"
        "_start: la a1, slot\n"
        "        sw zero, 0(a1)\n"
        "        ebreak\n");
    session.breakpoints().add_line(5, session.map());
    session.run();
    session.step(StepKind::Line);
    RV_CHECK_HEX(session.read_dmem_word(0), 0u);

    session.reset();
    RV_CHECK_HEX(session.read_dmem_word(0), 0x1234u);  // initial data restored
    RV_CHECK_EQ(session.breakpoints().breakpoints().size(), std::size_t{1});
    RV_CHECK(!session.can_reverse());  // history cleared
}

// ---- the shared command layer ---------------------------------------------

namespace {

/// Run a command and capture everything it printed. The GUI reaches the same
/// registry through its ':'-equivalent paths, so this covers both.
std::string run_command(DebugSession& session, const std::string& line) {
    std::string output;
    cmd::CmdContext context{session, [&output](const std::string& text) { output += text; }, {},
                            false};
    const cmd::CmdResult result = cmd::CommandRegistry::instance().execute(context, line);
    if (!result.ok) output += "error: " + result.error + "\n";
    return output;
}

}  // namespace

RV_TEST(session, commands_drive_the_same_engine_the_gui_does) {
    DebugSession session = make_session(
        "        li a0, 1\n"
        "        li a1, 2\n"
        "        ebreak\n");

    RV_CHECK_NE(run_command(session, "break 2").find("breakpoint 1 at line 2"), std::string::npos);
    run_command(session, "run");
    RV_CHECK_EQ(*session.current_line(), 2u);
    RV_CHECK_NE(run_command(session, "print a0").find("0x00000001"), std::string::npos);

    run_command(session, "step");
    RV_CHECK_HEX(reg(session, 11), 2u);
    run_command(session, "back");
    RV_CHECK_HEX(reg(session, 11), 0u);
}

RV_TEST(session, the_examine_spec_can_be_written_attached_or_separated) {
    DebugSession session = make_session(
        "        .data\n"
        "        .word 0x11223344\n"
        "        .text\n"
        "_start: ebreak\n");
    const std::string attached = run_command(session, "x/4xw 0");
    const std::string separated = run_command(session, "x /4xw 0");
    RV_CHECK_STR(attached, separated);
    RV_CHECK_NE(attached.find("11223344"), std::string::npos);
}

RV_TEST(session, unknown_commands_and_names_suggest_a_correction) {
    DebugSession session = make_session("loop:   ebreak\n");
    RV_CHECK_NE(run_command(session, "brek 1").find("did you mean 'break'"), std::string::npos);
    RV_CHECK_NE(run_command(session, "break lop").find("did you mean 'loop'"), std::string::npos);
    RV_CHECK_NE(run_command(session, "print zerp").find("did you mean 'zero'"), std::string::npos);
}
