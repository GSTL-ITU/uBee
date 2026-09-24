// CSR access rules and the trap mechanism.
#include "machine_fixture.hpp"

using namespace fixture;
using rv::isa::InstrId;
using rv::core::HaltReason;
using rv::core::TrapCause;

namespace {

constexpr rv::CsrAddr kMstatus = 0x300;
constexpr rv::CsrAddr kMtvec = 0x305;
constexpr rv::CsrAddr kMscratch = 0x340;
constexpr rv::CsrAddr kMepc = 0x341;
constexpr rv::CsrAddr kMcause = 0x342;
constexpr rv::CsrAddr kMtval = 0x343;
constexpr rv::CsrAddr kCycle = 0xc00;
constexpr rv::CsrAddr kInstret = 0xc02;
constexpr rv::CsrAddr kMcycle = 0xb00;
constexpr rv::CsrAddr kMinstret = 0xb02;
constexpr rv::CsrAddr kMisa = 0x301;

rv::u32 read_csr(Machine& m, rv::CsrAddr addr) { return m.hart().cpu().csr.read(addr); }
void poke_csr(Machine& m, rv::CsrAddr addr, rv::u32 value) {
    m.hart().cpu().csr.raw_write(addr, value);
}

}  // namespace

RV_TEST(csr_trap, csrrw_swaps_the_value) {
    Machine m;
    m.load({
        I(InstrId::ADDI, 1, 0, 0x55),
        CSR(InstrId::CSRRW, 2, kMscratch, 1),  // x2 = old mscratch, mscratch = x1
        CSR(InstrId::CSRRW, 3, kMscratch, 0),  // x3 = 0x55, mscratch = 0
        EBREAK(),
    });
    m.run();
    RV_CHECK_HEX(m.reg(2), 0u);
    RV_CHECK_HEX(m.reg(3), 0x55u);
    RV_CHECK_HEX(read_csr(m, kMscratch), 0u);
}

RV_TEST(csr_trap, csrrs_with_x0_source_does_not_write) {
    // `csrr rd, csr` is spelled `csrrs rd, csr, x0`, and it must be a pure
    // read -- the condition is the register *number*, not its value.
    Machine m;
    m.load({
        CSR(InstrId::CSRRS, 1, kMscratch, 0),  // pure read
        EBREAK(),
    });
    poke_csr(m, kMscratch, 0xabcd'0000u);
    m.run();
    RV_CHECK_HEX(m.reg(1), 0xabcd'0000u);
    RV_CHECK_HEX(read_csr(m, kMscratch), 0xabcd'0000u);
}

RV_TEST(csr_trap, csrrs_and_csrrc_set_and_clear_bits) {
    Machine m;
    m.load({
        I(InstrId::ADDI, 1, 0, 0x0f),
        CSR(InstrId::CSRRS, 0, kMscratch, 1),  // mscratch |= 0x0f
        I(InstrId::ADDI, 2, 0, 0x03),
        CSR(InstrId::CSRRC, 0, kMscratch, 2),  // mscratch &= ~0x03
        CSR(InstrId::CSRRS, 3, kMscratch, 0),
        EBREAK(),
    });
    m.run();
    RV_CHECK_HEX(m.reg(3), 0x0cu);
}

RV_TEST(csr_trap, immediate_forms_zero_extend) {
    // csrrwi takes a 5-bit *zero*-extended immediate. Sign-extending it would
    // make csrrwi with 0x1f write 0xffffffff.
    Machine m;
    m.load({
        CSRI(InstrId::CSRRWI, 0, kMscratch, 31),
        CSR(InstrId::CSRRS, 1, kMscratch, 0),
        EBREAK(),
    });
    m.run();
    RV_CHECK_HEX(m.reg(1), 31u);
}

RV_TEST(csr_trap, csrrsi_with_zero_immediate_does_not_write) {
    Machine m;
    m.load({
        CSRI(InstrId::CSRRSI, 1, kMscratch, 0),  // pure read
        EBREAK(),
    });
    poke_csr(m, kMscratch, 0x1234u);
    m.run();
    RV_CHECK_HEX(m.reg(1), 0x1234u);
    RV_CHECK_HEX(read_csr(m, kMscratch), 0x1234u);
}

RV_TEST(csr_trap, write_mask_protects_reserved_bits) {
    // mstatus has only MIE, MPIE and MPP writable; everything else is held at
    // its reset value.
    Machine m;
    m.load({
        I(InstrId::ADDI, 1, 0, -1),
        CSR(InstrId::CSRRW, 0, kMstatus, 1),  // try to set every bit
        EBREAK(),
    });
    m.run();
    RV_CHECK_HEX(read_csr(m, kMstatus), 0x1888u);
}

RV_TEST(csr_trap, writing_a_read_only_csr_is_illegal) {
    // `cycle` has address bits [11:10] == 0b11, marking it read-only.
    Machine m;
    m.load({
        CSR(InstrId::CSRRW, 0, kCycle, 0),
        EBREAK(),
    });
    const rv::core::StepOutcome outcome = m.step();
    RV_CHECK(outcome.trapped);
    RV_CHECK_EQ(static_cast<int>(outcome.cause), static_cast<int>(TrapCause::IllegalInstruction));
}

RV_TEST(csr_trap, reading_a_read_only_csr_is_fine) {
    Machine m;
    m.load({
        CSR(InstrId::CSRRS, 1, kCycle, 0),
        CSR(InstrId::CSRRS, 2, kCycle, 0),
        EBREAK(),
    });
    m.run();
    // The counter is refreshed before each instruction, so the second read sees
    // a later cycle than the first.
    RV_CHECK(m.reg(2) > m.reg(1));
}

RV_TEST(csr_trap, the_machine_counters_are_writable) {
    // Unlike the user-mode shadows at 0xc00, mcycle is genuinely writable, and
    // the write has to reach the hart's own counter -- otherwise the refresh
    // before the next instruction would overwrite it and `csrw mcycle` would
    // look like it worked while doing nothing.
    Machine m;
    m.load({
        I(InstrId::ADDI, 1, 0, 100),
        CSR(InstrId::CSRRW, 0, kMcycle, 1),  // mcycle = 100
        CSR(InstrId::CSRRS, 2, kMcycle, 0),
        EBREAK(),
    });
    m.run();
    // 100 as written, plus one for the csrw itself retiring.
    RV_CHECK_HEX(m.reg(2), 101u);
}

RV_TEST(csr_trap, the_machine_counters_read_the_same_values_as_their_shadows) {
    // mcycle and cycle are two views of one counter, not two counters.
    Machine m;
    m.load({
        CSR(InstrId::CSRRS, 1, kMcycle, 0),
        CSR(InstrId::CSRRS, 2, kCycle, 0),
        CSR(InstrId::CSRRS, 3, kMinstret, 0),
        CSR(InstrId::CSRRS, 4, kInstret, 0),
        EBREAK(),
    });
    m.run();
    RV_CHECK_HEX(m.reg(2), m.reg(1) + 1u);  // one cycle apart
    RV_CHECK_HEX(m.reg(4), m.reg(3) + 1u);  // one retirement apart
}

RV_TEST(csr_trap, writing_the_user_mode_counter_shadow_is_still_illegal) {
    // Adding a writable mcycle must not make `cycle` writable by accident:
    // read-only-ness is decided by the address bits, not by the pair.
    Machine m;
    m.load({
        CSR(InstrId::CSRRW, 0, kCycle, 0),
        EBREAK(),
    });
    const rv::core::StepOutcome outcome = m.step();
    RV_CHECK(outcome.trapped);
    RV_CHECK_EQ(static_cast<int>(outcome.cause), static_cast<int>(TrapCause::IllegalInstruction));
}

RV_TEST(csr_trap, misa_advertises_the_c_extension) {
    // MXL=1 (RV32) in bits 31:30, then C (bit 2), I (bit 8) and M (bit 12).
    Machine m;
    m.load({
        CSR(InstrId::CSRRS, 1, kMisa, 0),
        EBREAK(),
    });
    m.run();
    RV_CHECK_HEX(m.reg(1), 0x4000'1104u);
    RV_CHECK_HEX(m.reg(1) & 0x4u, 0x4u);  // the C bit specifically
}

RV_TEST(csr_trap, mepc_can_hold_a_two_mod_four_address) {
    // Before the C extension mepc's low two bits were held at zero. They cannot
    // be any more: a compressed instruction can sit at 2 mod 4, and masking
    // that bit off would resume a handler two bytes before the faulting
    // instruction -- in the middle of the one before it.
    Machine m;
    m.load({EBREAK()});
    m.hart().cpu().csr.write(kMepc, 0x0000'0006u);
    RV_CHECK_HEX(read_csr(m, kMepc), 0x0000'0006u);
    // Bit 0 is still held at zero: IALIGN is 16, not 8.
    m.hart().cpu().csr.write(kMepc, 0x0000'0007u);
    RV_CHECK_HEX(read_csr(m, kMepc), 0x0000'0006u);
}

RV_TEST(csr_trap, a_trap_from_a_compressed_instruction_records_its_own_address) {
    // mepc must point at the compressed instruction that faulted, so a handler
    // knows to advance it by 2 rather than 4 -- getting that wrong is what
    // makes an ecall handler swallow the instruction after the ecall.
    Machine m;
    m.load({
        // c.li a0, 1 at 0x00, then an ecall at 0x02 straddling the boundary.
        0x0073'4505u,
        0x0000'0000u,
        EBREAK(),
    });
    poke_csr(m, kMtvec, 0x08);
    m.step();                                        // c.li
    const rv::core::StepOutcome outcome = m.step();  // ecall
    RV_CHECK(outcome.trapped);
    RV_CHECK_HEX(read_csr(m, kMepc), 0x02u);         // the ecall's own address
}

RV_TEST(csr_trap, accessing_an_unimplemented_csr_is_illegal) {
    Machine m;
    m.load({
        CSR(InstrId::CSRRS, 1, 0x7c0, 0),  // not in the table
        EBREAK(),
    });
    const rv::core::StepOutcome outcome = m.step();
    RV_CHECK(outcome.trapped);
    RV_CHECK_EQ(static_cast<int>(outcome.cause), static_cast<int>(TrapCause::IllegalInstruction));
}

RV_TEST(csr_trap, ecall_enters_the_handler_and_records_state) {
    Machine m;
    m.load({
        I(InstrId::ADDI, 1, 0, 1),  // 0x00
        ECALL(),                    // 0x04
        I(InstrId::ADDI, 2, 0, 2),  // 0x08 -- skipped, we trap first
        I(InstrId::ADDI, 3, 0, 3),  // 0x0c -- the handler
        EBREAK(),
    });
    poke_csr(m, kMtvec, 0x0c);
    m.run();

    RV_CHECK_HEX(read_csr(m, kMepc), 0x04u);  // the faulting instruction, not pc+4
    RV_CHECK_HEX(read_csr(m, kMcause),
                 static_cast<rv::u32>(TrapCause::EnvironmentCallFromMMode));
    RV_CHECK_HEX(m.reg(2), 0u);  // handler ran instead
    RV_CHECK_HEX(m.reg(3), 3u);
}

RV_TEST(csr_trap, trap_entry_saves_the_interrupt_enable_bit) {
    Machine m;
    m.load({ECALL(), EBREAK()});
    poke_csr(m, kMtvec, 0x04);
    poke_csr(m, kMstatus, 0x8);  // MIE set
    m.step();

    const rv::u32 mstatus = read_csr(m, kMstatus);
    RV_CHECK_HEX(rv::bit(mstatus, 7), 1u);   // MPIE <- old MIE
    RV_CHECK_HEX(rv::bit(mstatus, 3), 0u);   // MIE cleared
    RV_CHECK_HEX(rv::bits(mstatus, 12, 11), 3u);  // MPP = machine mode
}

RV_TEST(csr_trap, a_complete_trap_handler_round_trip) {
    // The full shape of a real handler: save mepc, step it past the faulting
    // instruction, and mret back. Returning without advancing mepc would
    // re-execute the ecall forever, which is the mistake this test pins down.
    Machine m;
    m.load({
        I(InstrId::ADDI, 1, 0, 1),             // 0x00
        ECALL(),                               // 0x04 -> handler at 0x10
        I(InstrId::ADDI, 2, 0, 2),             // 0x08 <- mret lands here
        EBREAK(),                              // 0x0c
        CSR(InstrId::CSRRS, 5, kMepc, 0),      // 0x10 handler: x5 = mepc
        I(InstrId::ADDI, 5, 5, 4),             // 0x14 skip the ecall
        CSR(InstrId::CSRRW, 0, kMepc, 5),      // 0x18 mepc = x5
        I(InstrId::ADDI, 3, 0, 3),             // 0x1c mark that we got here
        enc(InstrId::MRET, {}),                // 0x20 return
    });
    poke_csr(m, kMtvec, 0x10);
    m.run();

    RV_CHECK_HEX(m.reg(1), 1u);  // before the trap
    RV_CHECK_HEX(m.reg(3), 3u);  // inside the handler
    RV_CHECK_HEX(m.reg(2), 2u);  // after returning
    RV_CHECK_EQ(static_cast<int>(m.hart().halt_reason()), static_cast<int>(HaltReason::Ebreak));
}

RV_TEST(csr_trap, mret_restores_the_interrupt_enable_bit) {
    Machine m;
    m.load({
        enc(InstrId::MRET, {}),
        EBREAK(),
    });
    poke_csr(m, kMepc, 0x04);
    poke_csr(m, kMstatus, 0x80);  // MPIE set, MIE clear
    m.step();

    const rv::u32 mstatus = read_csr(m, kMstatus);
    RV_CHECK_HEX(rv::bit(mstatus, 3), 1u);  // MIE <- MPIE
    RV_CHECK_HEX(rv::bit(mstatus, 7), 1u);  // MPIE set to 1
}

RV_TEST(csr_trap, a_trap_with_no_handler_halts_instead_of_jumping_to_zero) {
    // mtvec is zero by default. Jumping there would silently restart the
    // program from the top, which is the single most confusing failure mode a
    // beginner can hit. Halt and say so instead.
    Machine m;
    m.load({ECALL(), EBREAK()});
    const rv::core::StepOutcome outcome = m.step();
    RV_CHECK(outcome.trapped);
    RV_CHECK_EQ(static_cast<int>(outcome.halt), static_cast<int>(HaltReason::UnhandledTrap));
    RV_CHECK(m.hart().halted());
    RV_CHECK_HEX(m.pc(), 0u);  // stays on the faulting instruction for the UI
}

RV_TEST(csr_trap, illegal_instruction_reports_the_offending_word_in_mtval) {
    Machine m;
    m.load({0x2000'3033u, EBREAK()});  // reserved funct7 for opcode 0x33
    poke_csr(m, kMtvec, 0x04);
    const rv::core::StepOutcome outcome = m.step();
    RV_CHECK(outcome.trapped);
    RV_CHECK_EQ(static_cast<int>(outcome.cause), static_cast<int>(TrapCause::IllegalInstruction));
    RV_CHECK_HEX(read_csr(m, kMtval), 0x2000'3033u);
}

RV_TEST(csr_trap, ebreak_can_trap_instead_of_halting) {
    Machine m;
    m.load({EBREAK(), I(InstrId::ADDI, 1, 0, 7)});
    m.hart().set_ebreak_behavior(rv::core::EbreakBehavior::TrapToMtvec);
    poke_csr(m, kMtvec, 0x04);
    const rv::core::StepOutcome outcome = m.step();
    RV_CHECK(outcome.trapped);
    RV_CHECK_EQ(static_cast<int>(outcome.cause), static_cast<int>(TrapCause::Breakpoint));
    RV_CHECK(!m.hart().halted());
}

RV_TEST(csr_trap, running_off_the_end_of_a_program_stops) {
    // Instruction memory past the program is zero, and an all-zero word is not
    // a valid encoding. Forgetting the final ebreak therefore stops with a
    // named illegal-instruction trap rather than executing garbage.
    Machine m;
    m.load({I(InstrId::ADDI, 1, 0, 1)});  // no ebreak
    m.run(20);
    RV_CHECK(m.hart().halted());
    RV_CHECK_EQ(static_cast<int>(m.hart().halt_reason()),
                static_cast<int>(HaltReason::UnhandledTrap));
    RV_CHECK_HEX(m.pc(), 0x04u);  // stopped at the zero word, not somewhere wild
}
