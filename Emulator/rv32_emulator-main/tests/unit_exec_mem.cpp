// Loads, stores, alignment traps and memory-mapped I/O.
#include "machine_fixture.hpp"

using namespace fixture;
using rv::isa::InstrId;
using rv::core::TrapCause;

RV_TEST(exec_mem, store_then_load_word) {
    Machine m;
    m.load({
        I(InstrId::ADDI, 1, 0, 0x123),  // x1 = 0x123
        I(InstrId::ADDI, 2, 0, 0x40),   // x2 = address
        S(InstrId::SW, 2, 1, 0),        // sw x1, 0(x2)
        I(InstrId::LW, 3, 2, 0),        // lw x3, 0(x2)
        EBREAK(),
    });
    m.run();
    RV_CHECK_HEX(m.reg(3), 0x123u);
    RV_CHECK_HEX(m.dmem_word(0x40), 0x123u);
}

RV_TEST(exec_mem, byte_and_half_sign_extension) {
    Machine m;
    m.load({
        I(InstrId::ADDI, 1, 0, 0x40),
        I(InstrId::LB, 2, 1, 0),   // signed byte:      0xffffff80
        I(InstrId::LBU, 3, 1, 0),  // unsigned byte:    0x00000080
        I(InstrId::LH, 4, 1, 0),   // signed halfword:  0xffff8080
        I(InstrId::LHU, 5, 1, 0),  // unsigned halfword:0x00008080
        EBREAK(),
    });
    // After load(), which resets the machine and clears data memory.
    m.set_dmem_word(0x40, 0x8080'8080u);
    m.run();
    RV_CHECK_HEX(m.reg(2), 0xffff'ff80u);
    RV_CHECK_HEX(m.reg(3), 0x0000'0080u);
    RV_CHECK_HEX(m.reg(4), 0xffff'8080u);
    RV_CHECK_HEX(m.reg(5), 0x0000'8080u);
}

RV_TEST(exec_mem, sub_word_stores_leave_neighbours_alone) {
    Machine m;
    m.load({
        I(InstrId::ADDI, 1, 0, 0x40),
        I(InstrId::ADDI, 2, 0, 0x11),
        S(InstrId::SB, 1, 2, 0),  // sb x2, 0(x1) -- only the lowest byte
        EBREAK(),
    });
    m.set_dmem_word(0x40, 0xaaaa'aaaau);
    m.run();
    // Little-endian: byte 0 is the least significant.
    RV_CHECK_HEX(m.dmem_word(0x40), 0xaaaa'aa11u);
}

RV_TEST(exec_mem, negative_offsets) {
    Machine m;
    m.load({
        I(InstrId::ADDI, 1, 0, 0x80),
        I(InstrId::ADDI, 2, 0, 7),
        S(InstrId::SW, 1, 2, -16),  // sw x2, -16(x1) -> address 0x70
        I(InstrId::LW, 3, 1, -16),
        EBREAK(),
    });
    m.run();
    RV_CHECK_HEX(m.reg(3), 7u);
    RV_CHECK_HEX(m.dmem_word(0x70), 7u);
}

RV_TEST(exec_mem, misaligned_word_access_traps) {
    // We trap rather than silently allowing it: on the FPGA core this emulator
    // is a reference for, a misaligned access is a real bug.
    Machine m;
    m.load({
        I(InstrId::ADDI, 1, 0, 0x41),  // deliberately not word aligned
        I(InstrId::LW, 2, 1, 0),
        EBREAK(),
    });
    m.step();
    const rv::core::StepOutcome outcome = m.step();
    RV_CHECK(outcome.trapped);
    RV_CHECK_EQ(static_cast<int>(outcome.cause), static_cast<int>(TrapCause::LoadAddressMisaligned));
    RV_CHECK_HEX(outcome.tval, 0x41u);
}

RV_TEST(exec_mem, misaligned_store_reports_the_store_cause) {
    Machine m;
    m.load({
        I(InstrId::ADDI, 1, 0, 0x42),
        S(InstrId::SW, 1, 0, 0),
        EBREAK(),
    });
    m.step();
    const rv::core::StepOutcome outcome = m.step();
    RV_CHECK(outcome.trapped);
    RV_CHECK_EQ(static_cast<int>(outcome.cause),
                static_cast<int>(TrapCause::StoreAddressMisaligned));
}

RV_TEST(exec_mem, out_of_range_access_faults) {
    Machine m;
    m.load({
        U(InstrId::LUI, 1, 0x0100'0000),  // far past the 16 KB of DMEM
        I(InstrId::LW, 2, 1, 0),
        EBREAK(),
    });
    m.step();
    const rv::core::StepOutcome outcome = m.step();
    RV_CHECK(outcome.trapped);
    RV_CHECK_EQ(static_cast<int>(outcome.cause), static_cast<int>(TrapCause::LoadAccessFault));
}

RV_TEST(exec_mem, uart_writes_reach_the_console) {
    Machine m;
    // 0xFFFF0000 built as lui 0xffff0 + sb. The lui operand is the already
    // shifted value, matching how the assembler will produce it.
    m.load({
        U(InstrId::LUI, 1, static_cast<rv::i32>(0xffff'0000u)),
        I(InstrId::ADDI, 2, 0, 'h'),
        S(InstrId::SB, 1, 2, 0),
        I(InstrId::ADDI, 2, 0, 'i'),
        S(InstrId::SB, 1, 2, 0),
        EBREAK(),
    });
    m.run();
    RV_CHECK_STR(m.hart().bus().uart_output(), "hi");
}

RV_TEST(exec_mem, led_register_round_trips) {
    Machine m;
    m.load({
        U(InstrId::LUI, 1, static_cast<rv::i32>(0xffff'0000u)),
        I(InstrId::ADDI, 2, 0, 0x2a),
        S(InstrId::SW, 1, 2, 0x10),  // leds at +0x10
        I(InstrId::LW, 3, 1, 0x10),
        EBREAK(),
    });
    m.run();
    RV_CHECK_HEX(m.hart().bus().leds()->value(), 0x2au);
    RV_CHECK_HEX(m.reg(3), 0x2au);
}

RV_TEST(exec_mem, imem_and_dmem_are_separate_address_spaces) {
    // The defining property of this machine: address 0 in IMEM and address 0
    // in DMEM are different storage. A program cannot read its own encoding.
    Machine m;
    m.load({
        I(InstrId::ADDI, 1, 0, 0),
        I(InstrId::LW, 2, 1, 0),  // lw x2, 0(x0) -- reads DMEM[0], not this program
        EBREAK(),
    });
    m.run();
    RV_CHECK_HEX(m.reg(2), 0u);  // DMEM is zeroed; IMEM[0] would have been non-zero
    RV_CHECK_NE(m.hart().imem().read_word_raw(0), 0u);
}
