// The five-stage view.
//
// Two things are being checked here, and the second matters more than the
// first. One: the picture is right -- instructions walk a stage per cycle, the
// pipe fills before anything commits, a taken branch discards what was fetched
// behind it. Two: drawing the picture did not touch the machine. A program run
// by ticks must end in exactly the state it reaches when run by instructions,
// cycle counter and all.
#include "dbg/pipeline.hpp"
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

std::string spelling(const Pipeline& pipeline, Stage stage) {
    return std::string(pipeline.mnemonic(stage));
}

/// Tick until the machine halts, with a bound so a broken model cannot hang
/// the suite.
u64 run_out(Pipeline& pipeline, const DebugSession& session, u64 limit = 20'000) {
    u64 ticks = 0;
    while (!session.halted() && ticks < limit) {
        pipeline.tick();
        ++ticks;
    }
    return ticks;
}

const char* const kStraightLine =
    "        addi a0, x0, 1\n"
    "        xor  a1, a0, a0\n"
    "        sll  a2, a0, a0\n"
    "        ebreak\n";

}  // namespace

RV_TEST(pipeline, the_pipe_fills_before_anything_commits) {
    // The point of the whole panel: an instruction is not finished the moment
    // it is fetched. Four cycles in, the machine has not moved at all.
    DebugSession session = make_session(kStraightLine);
    Pipeline pipeline(session);

    RV_CHECK(pipeline.drained());
    RV_CHECK_EQ(pipeline.ticks(), 0u);

    pipeline.tick();
    RV_CHECK_STR(spelling(pipeline, Stage::IF), "addi");
    RV_CHECK_EQ(session.cpu().instret, 0u);

    for (int i = 0; i < 3; ++i) pipeline.tick();
    RV_CHECK_STR(spelling(pipeline, Stage::MEM), "addi");
    RV_CHECK_EQ(session.cpu().instret, 0u);
    RV_CHECK_HEX(session.cpu().pc, 0u);

    // Fifth cycle: it reaches write-back, and that is where it becomes part of
    // the machine.
    pipeline.tick();
    RV_CHECK_STR(spelling(pipeline, Stage::WB), "addi");
    RV_CHECK_EQ(session.cpu().instret, 1u);
    RV_CHECK_HEX(session.cpu().x[10], 1u);
}

RV_TEST(pipeline, an_instruction_walks_one_stage_per_tick) {
    DebugSession session = make_session(kStraightLine);
    Pipeline pipeline(session);

    pipeline.tick();
    RV_CHECK_STR(spelling(pipeline, Stage::IF), "addi");

    pipeline.tick();
    RV_CHECK_STR(spelling(pipeline, Stage::ID), "addi");
    RV_CHECK_STR(spelling(pipeline, Stage::IF), "xor");

    pipeline.tick();
    RV_CHECK_STR(spelling(pipeline, Stage::EX), "addi");
    RV_CHECK_STR(spelling(pipeline, Stage::ID), "xor");
    RV_CHECK_STR(spelling(pipeline, Stage::IF), "sll");

    pipeline.tick();
    RV_CHECK_STR(spelling(pipeline, Stage::MEM), "addi");
    RV_CHECK_STR(spelling(pipeline, Stage::EX), "xor");

    pipeline.tick();
    RV_CHECK_STR(spelling(pipeline, Stage::WB), "addi");
}

RV_TEST(pipeline, a_bubble_has_no_spelling) {
    DebugSession session = make_session(kStraightLine);
    Pipeline pipeline(session);
    pipeline.tick();

    RV_CHECK(!pipeline.at(Stage::WB).valid);
    RV_CHECK_STR(spelling(pipeline, Stage::WB), "");
}

RV_TEST(pipeline, the_target_is_computed_in_ex_and_the_front_end_follows_it) {
    // EX is where the target adder is, so that is where the front-end learns
    // there is somewhere else to go. Which way the branch goes is not known
    // until it runs, so the guess is always-taken -- and here the guess is
    // right, which costs only the one fetch it turned around.
    DebugSession session = make_session(
        "        addi a0, x0, 0\n"
        "        beq  a0, x0, target\n"
        "        addi a1, x0, 99\n"    // never runs
        "        addi a2, x0, 98\n"    // never runs
        "target:\n"
        "        addi a3, x0, 7\n"
        "        ebreak\n");
    Pipeline pipeline(session);

    const auto target = session.lookup_symbol("target");
    RV_CHECK(target.has_value());

    // Four cycles in, the beq is in EX and the guess has already been made.
    for (int i = 0; i < 4; ++i) pipeline.tick();
    RV_CHECK_STR(spelling(pipeline, Stage::EX), "beq");
    RV_CHECK(pipeline.at(Stage::EX).predicted);
    RV_CHECK_HEX(pipeline.at(Stage::EX).predicted_pc, *target);
    // The instruction fetched from straight ahead is gone, and the next one
    // comes from the target.
    RV_CHECK(!pipeline.at(Stage::ID).valid);
    RV_CHECK_EQ(pipeline.last_flushed(), u8{1});
    RV_CHECK_HEX(pipeline.at(Stage::IF).pc, *target);

    // Two cycles later it retires and the guess turns out to have been right,
    // so nothing else is thrown away.
    for (int i = 0; i < 2; ++i) pipeline.tick();
    RV_CHECK_STR(spelling(pipeline, Stage::WB), "beq");
    RV_CHECK(pipeline.at(Stage::WB).redirected);
    RV_CHECK_EQ(pipeline.last_flushed(), u8{0});
    RV_CHECK(!pipeline.last_mispredicted());

    // And the path not taken never ran.
    run_out(pipeline, session);
    RV_CHECK_HEX(session.cpu().x[11], 0u);
    RV_CHECK_HEX(session.cpu().x[12], 0u);
    RV_CHECK_HEX(session.cpu().x[13], 7u);
}

RV_TEST(pipeline, a_branch_that_falls_through_is_a_misprediction) {
    // Always-taken is wrong about a branch that does not branch, and nothing
    // finds out until it runs at write-back.
    DebugSession session = make_session(
        "        addi a0, x0, 1\n"
        "        beq  a0, x0, skip\n"   // not taken
        "        addi a1, x0, 5\n"
        "skip:\n"
        "        ebreak\n");
    Pipeline pipeline(session);

    for (int i = 0; i < 4; ++i) pipeline.tick();
    RV_CHECK(pipeline.at(Stage::EX).predicted);  // guessed taken anyway

    for (int i = 0; i < 2; ++i) pipeline.tick();
    RV_CHECK_STR(spelling(pipeline, Stage::WB), "beq");
    RV_CHECK(!pipeline.at(Stage::WB).redirected);  // it fell through after all
    RV_CHECK(pipeline.last_mispredicted());
    RV_CHECK_EQ(pipeline.last_flushed(), u8{2});

    // The front-end is put back on the road it should have stayed on.
    RV_CHECK_HEX(pipeline.at(Stage::IF).pc, session.cpu().pc);

    run_out(pipeline, session);
    RV_CHECK_HEX(session.cpu().x[11], 5u);  // the fall-through really did run
}

RV_TEST(pipeline, ticking_leaves_exactly_the_machine_that_stepping_does) {
    // The guard on the whole feature. The pipeline is a picture of the machine,
    // not a re-timing of it: a program driven a cycle at a time must end in the
    // same state, down to the cycle counter that the timer and the CSRs read.
    const std::string program =
        "        li   a0, 10\n"
        "        li   a1, 0\n"
        "loop:\n"
        "        add  a1, a1, a0\n"
        "        addi a0, a0, -1\n"
        "        bne  a0, x0, loop\n"
        "        ebreak\n";

    DebugSession ticked = make_session(program);
    Pipeline pipeline(ticked);
    run_out(pipeline, ticked);

    DebugSession stepped = make_session(program);
    for (int i = 0; i < 20'000 && !stepped.halted(); ++i) {
        stepped.step(StepKind::Instruction);
    }

    RV_CHECK(ticked.halted());
    RV_CHECK(stepped.halted());
    RV_CHECK_HEX(ticked.cpu().pc, stepped.cpu().pc);
    RV_CHECK_EQ(ticked.cpu().cycle, stepped.cpu().cycle);
    RV_CHECK_EQ(ticked.cpu().instret, stepped.cpu().instret);
    for (RegIdx reg = 0; reg < kNumRegs; ++reg) {
        RV_CHECK_HEX(ticked.cpu().x[reg], stepped.cpu().x[reg]);
    }
    RV_CHECK_HEX(ticked.cpu().x[11], 55u);  // it really did add 10..1
}

RV_TEST(pipeline, the_pipeline_cycle_is_not_the_architectural_cycle) {
    // cpu().cycle drives the timer and mcycle, so it counts instructions, not
    // the cycles this picture spends filling and refilling.
    DebugSession session = make_session(kStraightLine);
    Pipeline pipeline(session);
    const u64 ticks = run_out(pipeline, session);

    RV_CHECK(ticks > session.cpu().cycle);
    RV_CHECK_EQ(pipeline.ticks(), ticks);
}

RV_TEST(pipeline, reverse_tick_undoes_the_commit_and_the_slots) {
    DebugSession session = make_session(kStraightLine);
    Pipeline pipeline(session);

    for (int i = 0; i < 5; ++i) pipeline.tick();
    RV_CHECK_EQ(session.cpu().instret, 1u);
    RV_CHECK_HEX(session.cpu().x[10], 1u);

    RV_CHECK(pipeline.reverse_tick());
    RV_CHECK_EQ(session.cpu().instret, 0u);
    RV_CHECK_HEX(session.cpu().x[10], 0u);
    RV_CHECK_EQ(pipeline.ticks(), 4u);
    // Back to the cycle before it wrote back.
    RV_CHECK(!pipeline.at(Stage::WB).valid);
    RV_CHECK_STR(spelling(pipeline, Stage::MEM), "addi");

    for (int i = 0; i < 4; ++i) RV_CHECK(pipeline.reverse_tick());
    RV_CHECK(pipeline.drained());
    RV_CHECK(!pipeline.can_reverse());
    RV_CHECK(!pipeline.reverse_tick());
}

RV_TEST(pipeline, ebreak_halts_and_the_pipe_drains_to_empty) {
    DebugSession session = make_session(kStraightLine);
    Pipeline pipeline(session);
    run_out(pipeline, session);

    RV_CHECK(session.halted());
    // Nothing more is fetched, and what is still in flight walks out.
    for (int i = 0; i < static_cast<int>(kNumStages); ++i) pipeline.tick();
    RV_CHECK(pipeline.drained());
}

RV_TEST(pipeline, a_compressed_instruction_keeps_its_own_spelling) {
    // The panel names what was written, not what it expands to -- the same rule
    // the encoding panel follows.
    DebugSession session = make_session(
        "        c.li a0, 3\n"
        "        ebreak\n");
    Pipeline pipeline(session);

    pipeline.tick();
    RV_CHECK_STR(spelling(pipeline, Stage::IF), "c.li");
    RV_CHECK_EQ(pipeline.at(Stage::IF).instr.length, u8{2});

    for (int i = 0; i < 4; ++i) pipeline.tick();
    RV_CHECK_HEX(session.cpu().x[10], 3u);
}

RV_TEST(pipeline, resync_puts_the_last_committed_instruction_in_write_back) {
    // Stepping a whole source line goes through the session, not through here.
    // The picture afterwards should say what just finished and what is coming,
    // rather than going blank.
    DebugSession session = make_session(kStraightLine);
    Pipeline pipeline(session);

    session.step(StepKind::Instruction);
    pipeline.resync();

    RV_CHECK_STR(spelling(pipeline, Stage::WB), "addi");
    RV_CHECK(pipeline.at(Stage::WB).committed);
    // The instruction at the pc is one cycle from retiring.
    RV_CHECK_HEX(pipeline.at(Stage::MEM).pc, session.cpu().pc);
    RV_CHECK_STR(spelling(pipeline, Stage::MEM), "xor");
    RV_CHECK_STR(spelling(pipeline, Stage::EX), "sll");
    RV_CHECK_EQ(pipeline.ticks(), 0u);

    // And the next cycle runs the instruction the pc is actually on.
    pipeline.tick();
    RV_CHECK_STR(spelling(pipeline, Stage::WB), "xor");
    RV_CHECK_EQ(session.cpu().instret, 2u);
}

RV_TEST(pipeline, the_diagram_records_a_row_per_cycle) {
    DebugSession session = make_session(kStraightLine);
    Pipeline pipeline(session);
    for (int i = 0; i < 5; ++i) pipeline.tick();

    // The first entry is where the pipe started; then one per cycle.
    RV_CHECK_EQ(pipeline.history().size(), std::size_t{6});
    RV_CHECK_EQ(pipeline.history().front().tick, 0u);
    RV_CHECK_EQ(pipeline.history().back().tick, 5u);
    RV_CHECK(pipeline.history().back().committed);   // the fifth cycle retired one
    RV_CHECK(!pipeline.history().front().committed);
}

RV_TEST(pipeline, resync_rebuilds_the_stages_around_the_pc) {
    // The bug this guards. A step driven through the session used to leave the
    // later stages empty, so anyone who drove with Step or Run rather than Step
    // Cycle never saw them. A real pipe is full at that moment; the view just
    // was not asking.
    DebugSession session = make_session(
        "        addi a0, x0, 1\n"
        "        xor  a1, a0, a0\n"
        "        sll  a2, a0, a0\n"
        "        or   a3, a0, a0\n"
        "        ebreak\n");
    Pipeline pipeline(session);

    for (int i = 0; i < 2; ++i) session.step(StepKind::Instruction);
    pipeline.resync();

    RV_CHECK_STR(spelling(pipeline, Stage::WB), "xor");   // the one that just retired
    RV_CHECK_STR(spelling(pipeline, Stage::MEM), "sll");  // and it is at the pc
    RV_CHECK_HEX(pipeline.at(Stage::MEM).pc, session.cpu().pc);
    RV_CHECK_STR(spelling(pipeline, Stage::EX), "or");
    RV_CHECK_STR(spelling(pipeline, Stage::ID), "ebreak");
    RV_CHECK(pipeline.at(Stage::IF).valid);
}

RV_TEST(pipeline, resync_leaves_write_back_empty_when_nothing_has_run) {
    DebugSession session = make_session(kStraightLine);
    Pipeline pipeline(session);

    pipeline.resync();

    RV_CHECK(!pipeline.at(Stage::WB).valid);  // nothing has retired yet
    RV_CHECK_STR(spelling(pipeline, Stage::MEM), "addi");
    RV_CHECK_HEX(pipeline.at(Stage::MEM).pc, session.cpu().pc);
}

RV_TEST(pipeline, resync_recovers_which_way_a_branch_went) {
    // Rebuilding the stage behind the pc has to rebuild the branch decision
    // too, or a branch would lose its colour the moment you pressed Step.
    DebugSession session = make_session(
        "        addi a0, x0, 0\n"
        "        beq  a0, x0, target\n"
        "        addi a1, x0, 99\n"
        "target:\n"
        "        addi a3, x0, 7\n"
        "        ebreak\n");
    Pipeline pipeline(session);

    for (int i = 0; i < 2; ++i) session.step(StepKind::Instruction);
    pipeline.resync();

    RV_CHECK_STR(spelling(pipeline, Stage::WB), "beq");
    RV_CHECK(pipeline.at(Stage::WB).redirected);
}

RV_TEST(pipeline, an_unconditional_jump_is_never_mispredicted) {
    // Always-taken is exactly right about a jump, every time.
    DebugSession session = make_session(
        "        addi a0, x0, 1\n"
        "        j    onward\n"
        "        addi a1, x0, 99\n"   // never runs
        "onward:\n"
        "        addi a2, x0, 7\n"
        "        ebreak\n");
    Pipeline pipeline(session);

    for (int i = 0; i < 4; ++i) pipeline.tick();
    RV_CHECK(pipeline.at(Stage::EX).predicted);
    RV_CHECK_HEX(pipeline.at(Stage::EX).predicted_pc, *session.lookup_symbol("onward"));

    for (int i = 0; i < 2; ++i) pipeline.tick();
    RV_CHECK(!pipeline.last_mispredicted());

    run_out(pipeline, session);
    RV_CHECK_HEX(session.cpu().x[11], 0u);
    RV_CHECK_HEX(session.cpu().x[12], 7u);
}

RV_TEST(pipeline, a_register_jump_is_not_guessed_at_all) {
    // jalr's target is in a register, and a register is not read until the
    // instruction runs -- so there is nothing for EX to compute and nothing to
    // guess. It redirects at write-back and throws away what is behind it.
    DebugSession session = make_session(
        "        la   a0, onward\n"
        "        jalr x0, a0, 0\n"
        "        addi a1, x0, 99\n"   // never runs
        "onward:\n"
        "        addi a2, x0, 7\n"
        "        ebreak\n");
    Pipeline pipeline(session);

    // la is two instructions, so jalr reaches EX a cycle later than a one-word
    // first line would put it.
    for (int i = 0; i < 5; ++i) pipeline.tick();
    RV_CHECK_STR(spelling(pipeline, Stage::EX), "jalr");
    RV_CHECK(!pipeline.at(Stage::EX).predicted);

    run_out(pipeline, session);
    RV_CHECK_HEX(session.cpu().x[11], 0u);
    RV_CHECK_HEX(session.cpu().x[12], 7u);
}

RV_TEST(pipeline, compressed_control_flow_is_guessed_the_same_way) {
    // Two-byte instructions, so the addresses step by 2 and the fall-through
    // is pc + 2 -- but the target is still pc plus the immediate, and nothing
    // about the guess changes except the arithmetic. Getting the width wrong
    // would show up here as a branch that never seems to be predicted right.
    DebugSession session = make_session(
        "        c.li   a0, 2\n"
        "loop:\n"
        "        c.addi a0, -1\n"
        "        c.bnez a0, loop\n"
        "        c.j    done\n"
        "        c.li   a1, 31\n"   // never runs
        "done:\n"
        "        c.li   a2, 7\n"
        "        ebreak\n");
    Pipeline pipeline(session);

    const auto loop = session.lookup_symbol("loop");
    RV_CHECK(loop.has_value());

    // Five cycles puts the c.bnez in EX with its target already computed.
    for (int i = 0; i < 5; ++i) pipeline.tick();
    RV_CHECK_STR(spelling(pipeline, Stage::EX), "c.bnez");
    RV_CHECK_EQ(pipeline.at(Stage::EX).instr.length, u8{2});
    RV_CHECK(pipeline.at(Stage::EX).predicted);
    RV_CHECK_HEX(pipeline.at(Stage::EX).predicted_pc, *loop);

    // Two more and it retires, taken, with the guess right.
    for (int i = 0; i < 2; ++i) pipeline.tick();
    RV_CHECK_STR(spelling(pipeline, Stage::WB), "c.bnez");
    RV_CHECK(pipeline.at(Stage::WB).redirected);
    RV_CHECK(!pipeline.last_mispredicted());

    run_out(pipeline, session);
    RV_CHECK_HEX(session.cpu().x[10], 0u);   // the loop ran itself out
    RV_CHECK_HEX(session.cpu().x[11], 0u);   // and c.j skipped the c.li
    RV_CHECK_HEX(session.cpu().x[12], 7u);
}

RV_TEST(pipeline, ticking_matches_stepping_for_compressed_control_flow) {
    // The same guard as for the 32-bit case, on a program that is all
    // 16-bit instructions and all branches and jumps.
    const std::string program =
        "        c.li   a0, 5\n"
        "        c.li   a1, 0\n"
        "loop:\n"
        "        c.add  a1, a0\n"
        "        c.addi a0, -1\n"
        "        c.bnez a0, loop\n"
        "        c.j    done\n"
        "        c.li   a1, 31\n"
        "done:\n"
        "        c.li   a2, 7\n"
        "        ebreak\n";

    DebugSession ticked = make_session(program);
    Pipeline pipeline(ticked);
    run_out(pipeline, ticked);

    DebugSession stepped = make_session(program);
    for (int i = 0; i < 20'000 && !stepped.halted(); ++i) {
        stepped.step(StepKind::Instruction);
    }

    RV_CHECK_HEX(ticked.cpu().pc, stepped.cpu().pc);
    RV_CHECK_EQ(ticked.cpu().cycle, stepped.cpu().cycle);
    RV_CHECK_EQ(ticked.cpu().instret, stepped.cpu().instret);
    for (RegIdx reg = 0; reg < kNumRegs; ++reg) {
        RV_CHECK_HEX(ticked.cpu().x[reg], stepped.cpu().x[reg]);
    }
    RV_CHECK_HEX(ticked.cpu().x[11], 15u);  // 5+4+3+2+1
}
