// Branches, jumps and the link register.
#include "machine_fixture.hpp"

using namespace fixture;
using rv::isa::InstrId;

RV_TEST(exec_branch, beq_taken_and_not_taken) {
    Machine m;
    m.load({
        I(InstrId::ADDI, 1, 0, 5),   // 0x00
        I(InstrId::ADDI, 2, 0, 5),   // 0x04
        B(InstrId::BEQ, 1, 2, 8),    // 0x08: equal -> skip the next instruction
        I(InstrId::ADDI, 3, 0, 99),  // 0x0c: must not execute
        I(InstrId::ADDI, 4, 0, 7),   // 0x10
        EBREAK(),
    });
    m.run();
    RV_CHECK_HEX(m.reg(3), 0u);
    RV_CHECK_HEX(m.reg(4), 7u);
}

RV_TEST(exec_branch, signed_and_unsigned_comparisons_differ) {
    Machine m;
    m.load({
        I(InstrId::ADDI, 1, 0, -1),  // 0xffffffff
        I(InstrId::ADDI, 2, 0, 1),
        B(InstrId::BLT, 1, 2, 8),    // signed: -1 < 1, taken
        I(InstrId::ADDI, 3, 0, 1),   // skipped
        B(InstrId::BLTU, 1, 2, 8),   // unsigned: huge < 1 is false, not taken
        I(InstrId::ADDI, 4, 0, 1),   // executed
        EBREAK(),
    });
    m.run();
    RV_CHECK_HEX(m.reg(3), 0u);
    RV_CHECK_HEX(m.reg(4), 1u);
}

RV_TEST(exec_branch, backward_branch_forms_a_loop) {
    // Count down from 3, proving negative branch offsets work. A wrong sign bit
    // in the B-type immediate turns this into an infinite loop or a wild jump.
    Machine m;
    m.load({
        I(InstrId::ADDI, 1, 0, 3),    // 0x00: counter
        I(InstrId::ADDI, 2, 0, 0),    // 0x04: accumulator
        R(InstrId::ADD, 2, 2, 1),     // 0x08: acc += counter
        I(InstrId::ADDI, 1, 1, -1),   // 0x0c: counter -= 1
        B(InstrId::BNE, 1, 0, -8),    // 0x10: loop while counter != 0
        EBREAK(),                     // 0x14
    });
    m.run();
    RV_CHECK_HEX(m.reg(1), 0u);
    RV_CHECK_HEX(m.reg(2), 6u);  // 3 + 2 + 1
}

RV_TEST(exec_branch, jal_links_and_jumps) {
    Machine m;
    m.load({
        J(InstrId::JAL, 1, 8),       // 0x00: jump to 0x08, ra = 0x04
        I(InstrId::ADDI, 2, 0, 99),  // 0x04: skipped
        EBREAK(),                    // 0x08
    });
    m.run();
    RV_CHECK_HEX(m.reg(1), 0x04u);
    RV_CHECK_HEX(m.reg(2), 0u);
}

RV_TEST(exec_branch, jalr_clears_the_low_bit_of_the_target) {
    // The spec requires the computed address to have its least significant bit
    // cleared. Without that, an odd base leaves the pc odd, which faults at
    // IALIGN=16 just as it did at 32 -- clearing the bit is what makes
    // `jalr rd, rs, offset` usable with a tagged pointer in rs.
    Machine m;
    m.load({
        I(InstrId::ADDI, 1, 0, 9),   // 0x00: x1 = 9 (odd)
        I(InstrId::JALR, 0, 1, -1),  // 0x04: (9 + -1) & ~1 = 8
        I(InstrId::ADDI, 2, 0, 99),  // 0x08: this is where we land
        EBREAK(),                    // 0x0c
    });
    m.run();
    RV_CHECK_HEX(m.reg(2), 99u);
}

RV_TEST(exec_branch, jalr_computes_the_target_before_linking) {
    // `jalr ra, ra, 0` must jump to the *old* ra. Writing the link register
    // first would make it jump to the instruction after itself.
    Machine m;
    m.load({
        I(InstrId::ADDI, 1, 0, 12),  // 0x00: x1 = 0x0c
        I(InstrId::JALR, 1, 1, 0),   // 0x04: jump to 0x0c, then x1 = 0x08
        I(InstrId::ADDI, 2, 0, 1),   // 0x08: skipped
        I(InstrId::ADDI, 3, 0, 2),   // 0x0c: landing site
        EBREAK(),
    });
    m.run();
    RV_CHECK_HEX(m.reg(3), 2u);
    RV_CHECK_HEX(m.reg(2), 0u);
    RV_CHECK_HEX(m.reg(1), 0x08u);
}

RV_TEST(exec_branch, a_target_two_past_a_word_boundary_is_legal) {
    // This used to trap. With the C extension present IALIGN is 16 rather than
    // 32, so a target of 2 mod 4 is an ordinary address -- it is where the
    // second of two compressed instructions in a word lives.
    Machine m;
    m.load({
        I(InstrId::ADDI, 1, 0, 10),  // 0x00: x1 = 0x0a, which is 2 mod 4
        I(InstrId::JALR, 0, 1, 0),   // 0x04: jump to 0x0a
        I(InstrId::ADDI, 2, 0, 1),   // 0x08: the low half is skipped over...
        EBREAK(),                    // 0x0c
    });
    // 0x0a is the upper half of the word at 0x08. Put a c.li there, so landing
    // mid-word finds a real instruction rather than the tail of another one.
    m.hart().imem().write_word_raw(0x08, 0x4589'0093u);  // 0x0a: c.li a1, 2
    m.step();
    const rv::core::StepOutcome outcome = m.step();
    RV_CHECK(!outcome.trapped);
    RV_CHECK_HEX(m.pc(), 0x0au);
}

RV_TEST(exec_branch, an_odd_pc_still_traps) {
    // IALIGN is 16, not 8. Nothing a program can execute reaches an odd pc --
    // jal and branch offsets are always even and jalr clears the low bit -- but
    // a debugger can set one, and it must fault rather than fetch a byte-offset
    // instruction.
    Machine m;
    m.load({EBREAK()});
    m.hart().cpu().pc = 0x03;
    const rv::core::StepOutcome outcome = m.step();
    RV_CHECK(outcome.trapped);
    RV_CHECK_EQ(static_cast<int>(outcome.cause),
                static_cast<int>(rv::core::TrapCause::InstructionAddressMisaligned));
}

RV_TEST(exec_branch, branch_with_rd_x0_does_not_link) {
    // `jal x0, offset` is a plain jump; the link must be discarded.
    Machine m;
    m.load({
        J(InstrId::JAL, 0, 8),
        I(InstrId::ADDI, 1, 0, 99),
        EBREAK(),
    });
    m.run();
    RV_CHECK_HEX(m.reg(0), 0u);
    RV_CHECK_HEX(m.reg(1), 0u);
}
