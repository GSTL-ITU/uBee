// Interrupts.
//
// The design constraint that shapes all of this: time is the cycle counter,
// not a wall clock. That is what keeps interrupts reproducible -- the same
// program takes the same interrupt on the same cycle every run, so the
// self-checking .s programs stay meaningful and reverse stepping stays exact.
#include "dbg/history.hpp"
#include "machine_fixture.hpp"

using namespace fixture;
using rv::core::HaltReason;
using rv::core::InterruptCause;
using rv::isa::InstrId;

namespace {

constexpr rv::CsrAddr kMstatus = 0x300;
constexpr rv::CsrAddr kMie = 0x304;
constexpr rv::CsrAddr kMtvec = 0x305;
constexpr rv::CsrAddr kMepc = 0x341;
constexpr rv::CsrAddr kMcause = 0x342;
constexpr rv::CsrAddr kMip = 0x344;

rv::u32 read_csr(Machine& m, rv::CsrAddr addr) { return m.hart().cpu().csr.read(addr); }
void poke_csr(Machine& m, rv::CsrAddr addr, rv::u32 value) {
    m.hart().cpu().csr.raw_write(addr, value);
}

/// Enable interrupts globally and unmask the given lines.
void enable(Machine& m, rv::u32 lines) {
    poke_csr(m, kMstatus, 0x8);  // MIE
    poke_csr(m, kMie, lines);
}

}  // namespace

RV_TEST(interrupt, the_timer_line_asserts_when_mtime_reaches_mtimecmp) {
    Machine m;
    m.load({EBREAK()});
    m.hart().bus().timer()->set_compare(5);

    RV_CHECK_HEX(m.hart().bus().pending_interrupts(4), 0u);
    RV_CHECK_HEX(m.hart().bus().pending_interrupts(5), rv::core::kIrqTimer);
    RV_CHECK_HEX(m.hart().bus().pending_interrupts(99), rv::core::kIrqTimer);
}

RV_TEST(interrupt, the_timer_resets_disarmed) {
    // A program that never touches the timer must never see an interrupt from
    // it, however long it runs.
    Machine m;
    m.load({EBREAK()});
    RV_CHECK(!m.hart().bus().timer()->armed());
    RV_CHECK_HEX(m.hart().bus().pending_interrupts(0xffff'ffffu), 0u);
}

RV_TEST(interrupt, a_timer_interrupt_transfers_to_mtvec) {
    Machine m;
    m.load({
        I(InstrId::ADDI, 1, 0, 1),   // 0x00
        I(InstrId::ADDI, 2, 0, 2),   // 0x04
        I(InstrId::ADDI, 3, 0, 3),   // 0x08
        I(InstrId::ADDI, 4, 0, 4),   // 0x0c handler
        EBREAK(),
    });
    poke_csr(m, kMtvec, 0x0c);
    enable(m, rv::core::kIrqTimer);
    m.hart().bus().timer()->set_compare(2);   // fires before the third instruction

    m.step();  // cycle 0 -> 1
    m.step();  // cycle 1 -> 2
    const rv::core::StepOutcome outcome = m.step();

    RV_CHECK(outcome.interrupted);
    RV_CHECK_EQ(static_cast<int>(outcome.interrupt), static_cast<int>(InterruptCause::MachineTimer));
    RV_CHECK_HEX(m.pc(), 0x0cu);
    RV_CHECK_HEX(m.reg(3), 0u);  // the pre-empted instruction did not run
}

RV_TEST(interrupt, mcause_marks_an_interrupt_and_mepc_points_at_the_unrun_instruction) {
    // The difference from an exception: mepc is the instruction that has *not*
    // executed, so mret resumes it rather than skipping it. A handler that
    // adjusted mepc the way an ecall handler must would drop an instruction.
    Machine m;
    m.load({
        I(InstrId::ADDI, 1, 0, 1),
        I(InstrId::ADDI, 2, 0, 2),
        I(InstrId::ADDI, 3, 0, 3),  // 0x08: pre-empted
        EBREAK(),
    });
    poke_csr(m, kMtvec, 0x0c);
    enable(m, rv::core::kIrqTimer);
    m.hart().bus().timer()->set_compare(2);

    m.step();
    m.step();
    m.step();

    RV_CHECK_HEX(read_csr(m, kMepc), 0x08u);
    RV_CHECK_HEX(read_csr(m, kMcause), rv::core::kMcauseInterrupt | 7u);
}

RV_TEST(interrupt, entry_disables_interrupts_and_mret_puts_them_back) {
    // Without this a handler would be interrupted by the very line it is
    // servicing, immediately and forever.
    Machine m;
    m.load({
        I(InstrId::ADDI, 1, 0, 1),
        I(InstrId::ADDI, 2, 0, 2),
        enc(InstrId::MRET, {}),  // 0x08 handler
        EBREAK(),
    });
    poke_csr(m, kMtvec, 0x08);
    enable(m, rv::core::kIrqTimer);
    m.hart().bus().timer()->set_compare(1);

    m.step();  // the first instruction runs, cycle becomes 1
    m.step();  // interrupt taken

    RV_CHECK_HEX(rv::bit(read_csr(m, kMstatus), 3), 0u);  // MIE cleared on entry
    RV_CHECK_HEX(rv::bit(read_csr(m, kMstatus), 7), 1u);  // and saved in MPIE

    m.step();  // mret
    RV_CHECK_HEX(rv::bit(read_csr(m, kMstatus), 3), 1u);  // MIE restored
}

RV_TEST(interrupt, mret_resumes_the_instruction_the_interrupt_pre_empted) {
    Machine m;
    m.load({
        I(InstrId::ADDI, 1, 0, 1),   // 0x00
        I(InstrId::ADDI, 2, 0, 2),   // 0x04 <- pre-empted, must still run
        EBREAK(),                    // 0x08
        I(InstrId::ADDI, 5, 0, 9),   // 0x0c handler marks itself
        enc(InstrId::MRET, {}),      // 0x10
    });
    poke_csr(m, kMtvec, 0x0c);
    enable(m, rv::core::kIrqTimer);
    m.hart().bus().timer()->set_compare(1);
    // Disarm on entry would be the program's job; here the handler runs once
    // and then interrupts stay masked because mret restores MIE only after the
    // timer has already been acknowledged below.
    m.step();
    m.step();  // interrupt
    m.hart().bus().timer()->set_compare(rv::core::TimerDevice::kDisarmed);  // acknowledge
    m.run();

    RV_CHECK_HEX(m.reg(5), 9u);  // the handler ran
    RV_CHECK_HEX(m.reg(2), 2u);  // and the pre-empted instruction was not lost
}

RV_TEST(interrupt, a_masked_line_does_not_fire) {
    Machine m;
    m.load({I(InstrId::ADDI, 1, 0, 1), I(InstrId::ADDI, 2, 0, 2), EBREAK()});
    poke_csr(m, kMtvec, 0x0c);
    m.hart().bus().timer()->set_compare(0);

    // mstatus.MIE clear: globally masked.
    poke_csr(m, kMie, rv::core::kIrqTimer);
    m.run();
    RV_CHECK_HEX(m.reg(2), 2u);
    RV_CHECK_EQ(static_cast<int>(m.hart().halt_reason()), static_cast<int>(HaltReason::Ebreak));

    // mie clear for that line: individually masked.
    Machine second;
    second.load({I(InstrId::ADDI, 1, 0, 1), I(InstrId::ADDI, 2, 0, 2), EBREAK()});
    poke_csr(second, kMtvec, 0x0c);
    poke_csr(second, kMstatus, 0x8);
    second.hart().bus().timer()->set_compare(0);
    second.run();
    RV_CHECK_HEX(second.reg(2), 2u);
}

RV_TEST(interrupt, mip_reflects_the_device_lines_and_is_read_only) {
    Machine m;
    m.load({CSR(InstrId::CSRRS, 1, kMip, 0), EBREAK()});
    m.hart().bus().timer()->set_compare(0);  // asserted from cycle 0
    m.run();
    RV_CHECK_HEX(m.reg(1) & rv::core::kIrqTimer, rv::core::kIrqTimer);

    // Writing it is legal (it is not in the read-only address range) but has
    // no effect: the value is recomputed from the devices before every step.
    Machine second;
    second.load({
        I(InstrId::ADDI, 1, 0, -1),
        CSR(InstrId::CSRRW, 0, kMip, 1),
        CSR(InstrId::CSRRS, 2, kMip, 0),
        EBREAK(),
    });
    second.run();
    RV_CHECK_HEX(second.reg(2), 0u);
}

RV_TEST(interrupt, software_and_external_lines_come_from_mmio) {
    Machine m;
    m.load({
        U(InstrId::LUI, 1, static_cast<rv::i32>(0xffff'0000u)),
        I(InstrId::ADDI, 2, 0, 1),
        S(InstrId::SW, 1, 2, 0x50),  // raise the software request
        I(InstrId::ADDI, 3, 0, 7),   // handler, at 0x0c
        EBREAK(),
    });
    poke_csr(m, kMtvec, 0x0c);
    enable(m, rv::core::kIrqSoftware);
    m.run();

    RV_CHECK_HEX(m.reg(3), 7u);
    RV_CHECK_HEX(read_csr(m, kMcause), rv::core::kMcauseInterrupt | 3u);
}

RV_TEST(interrupt, external_outranks_software_which_outranks_timer) {
    // Priority is fixed by the spec, and it matters when several arrive at once.
    Machine m;
    m.load({I(InstrId::ADDI, 1, 0, 1), EBREAK(), EBREAK(), I(InstrId::ADDI, 2, 0, 2)});
    poke_csr(m, kMtvec, 0x0c);
    enable(m, rv::core::kIrqAll);
    m.hart().bus().timer()->set_compare(0);
    m.hart().bus().irq()->set_requests(rv::core::kIrqSoftware | rv::core::kIrqExternal);

    m.step();
    RV_CHECK_HEX(read_csr(m, kMcause), rv::core::kMcauseInterrupt | 11u);  // external
}

RV_TEST(interrupt, an_interrupt_with_no_handler_installed_halts_and_says_so) {
    // Enabling a line without writing mtvec is a mistake worth naming, not a
    // jump to address zero that silently restarts the program.
    Machine m;
    m.load({I(InstrId::ADDI, 1, 0, 1), EBREAK()});
    enable(m, rv::core::kIrqTimer);
    m.hart().bus().timer()->set_compare(0);

    const rv::core::StepOutcome outcome = m.step();
    RV_CHECK(outcome.interrupted);
    RV_CHECK_EQ(static_cast<int>(outcome.halt), static_cast<int>(HaltReason::UnhandledTrap));
}

// ---- reverse stepping across an interrupt ---------------------------------

RV_TEST(interrupt, reverse_stepping_undoes_an_interrupt_exactly) {
    // The riskiest interaction in the whole design. Taking an interrupt writes
    // four CSRs at once, so it takes the full-snapshot path; and the timer line
    // is a function of the cycle counter, so restoring the cycle has to restore
    // the pending state with it. If either were wrong, stepping back over an
    // interrupt would leave the machine in a state it was never in.
    Machine m;
    m.load({
        I(InstrId::ADDI, 1, 0, 1),   // 0x00
        I(InstrId::ADDI, 2, 0, 2),   // 0x04
        I(InstrId::ADDI, 3, 0, 3),   // 0x08 pre-empted
        I(InstrId::ADDI, 4, 0, 4),   // 0x0c handler
        EBREAK(),
    });
    poke_csr(m, kMtvec, 0x0c);
    enable(m, rv::core::kIrqTimer);
    m.hart().bus().timer()->set_compare(2);

    rv::dbg::StepHistory history(64);
    const auto step_recording = [&] {
        const rv::core::StepOutcome outcome = m.hart().step();
        history.record(outcome, m.hart());
        return outcome;
    };

    step_recording();
    step_recording();
    const rv::core::CpuState before = m.hart().cpu();

    const rv::core::StepOutcome taken = step_recording();
    RV_CHECK(taken.interrupted);
    RV_CHECK_HEX(m.pc(), 0x0cu);

    RV_CHECK(history.undo(m.hart()));

    RV_CHECK_HEX(m.pc(), before.pc);
    RV_CHECK_HEX(read_csr(m, kMepc), before.csr.read(kMepc));
    RV_CHECK_HEX(read_csr(m, kMcause), before.csr.read(kMcause));
    RV_CHECK_HEX(read_csr(m, kMstatus), before.csr.read(kMstatus));
    RV_CHECK_EQ(m.hart().cpu().cycle, before.cycle);

    // And going forward again takes the same interrupt on the same cycle,
    // which is the property that makes any of this reproducible.
    const rv::core::StepOutcome again = m.hart().step();
    RV_CHECK(again.interrupted);
    RV_CHECK_EQ(static_cast<int>(again.interrupt), static_cast<int>(taken.interrupt));
}

RV_TEST(interrupt, reverse_stepping_restores_a_timer_the_handler_rearmed) {
    // mtimecmp lives in a device, so the delta has to carry device state too --
    // otherwise stepping back would leave the timer armed for a moment that has
    // not happened yet.
    Machine m;
    m.load({
        U(InstrId::LUI, 1, static_cast<rv::i32>(0xffff'0000u)),
        I(InstrId::ADDI, 2, 0, 100),
        S(InstrId::SW, 1, 2, 0x40),  // mtimecmp = 100
        EBREAK(),
    });

    rv::dbg::StepHistory history(64);
    const auto step_recording = [&] {
        const rv::core::StepOutcome outcome = m.hart().step();
        history.record(outcome, m.hart());
    };

    step_recording();
    step_recording();
    RV_CHECK(!m.hart().bus().timer()->armed());

    step_recording();  // the store arms it
    RV_CHECK_HEX(m.hart().bus().timer()->compare(), 100u);

    history.undo(m.hart());
    RV_CHECK(!m.hart().bus().timer()->armed());
}

// ---- wfi -------------------------------------------------------------------

RV_TEST(interrupt, wfi_jumps_forward_to_the_next_timer_event) {
    // Time is the cycle counter, so "wait" means skip ahead rather than burn
    // instructions in a spin loop. A spin would work, but it would make the
    // cycle count meaningless and the reverse-step history useless.
    Machine m;
    m.load({
        enc(InstrId::WFI, {}),      // 0x00
        I(InstrId::ADDI, 1, 0, 1),  // 0x04
        EBREAK(),
    });
    poke_csr(m, kMtvec, 0x08);
    poke_csr(m, kMie, rv::core::kIrqTimer);
    m.hart().bus().timer()->set_compare(500);

    m.step();  // the wfi
    RV_CHECK(m.hart().cpu().cycle >= 500);
    RV_CHECK_HEX(m.pc(), 0x04u);  // wfi itself retires normally
}

RV_TEST(interrupt, wfi_retires_immediately_when_something_is_already_pending) {
    Machine m;
    m.load({enc(InstrId::WFI, {}), EBREAK()});
    poke_csr(m, kMie, rv::core::kIrqTimer);
    m.hart().bus().timer()->set_compare(0);  // already asserted

    m.step();
    RV_CHECK_EQ(m.hart().cpu().cycle, rv::u64{1});  // no skipping
}

RV_TEST(interrupt, wfi_with_nothing_armed_halts_rather_than_hanging) {
    Machine m;
    m.load({enc(InstrId::WFI, {}), EBREAK()});
    m.step();
    RV_CHECK(m.hart().halted());
    RV_CHECK_EQ(static_cast<int>(m.hart().halt_reason()),
                static_cast<int>(HaltReason::WaitingForever));
}

RV_TEST(interrupt, wfi_wakes_on_a_line_that_mstatus_has_globally_masked) {
    // wfi observes mie alone: the spec has it resume on a pending enabled
    // interrupt whether or not mstatus.MIE would let it be taken.
    Machine m;
    m.load({enc(InstrId::WFI, {}), I(InstrId::ADDI, 1, 0, 5), EBREAK()});
    poke_csr(m, kMstatus, 0);  // globally disabled
    poke_csr(m, kMie, rv::core::kIrqTimer);
    m.hart().bus().timer()->set_compare(0);

    m.run();
    RV_CHECK_HEX(m.reg(1), 5u);  // it continued rather than halting
    RV_CHECK_EQ(static_cast<int>(m.hart().halt_reason()), static_cast<int>(HaltReason::Ebreak));
}
