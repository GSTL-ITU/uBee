// Arithmetic, logic and shift semantics.
//
// Each check cites the rule it enforces. With no reference toolchain installed
// the specification is the only oracle, so the reasoning has to be visible in
// the test rather than hidden behind "it matches gcc".
#include <limits>

#include "machine_fixture.hpp"

using namespace fixture;
using rv::isa::InstrId;

RV_TEST(exec_alu, addi_and_add) {
    Machine m;
    m.load({
        I(InstrId::ADDI, 1, 0, 5),   // addi x1, x0, 5
        I(InstrId::ADDI, 2, 0, 3),   // addi x2, x0, 3
        R(InstrId::ADD, 3, 1, 2),    // add  x3, x1, x2
        R(InstrId::SUB, 4, 1, 2),    // sub  x4, x1, x2
        EBREAK(),
    });
    m.run();
    RV_CHECK_HEX(m.reg(1), 5u);
    RV_CHECK_HEX(m.reg(2), 3u);
    RV_CHECK_HEX(m.reg(3), 8u);
    RV_CHECK_HEX(m.reg(4), 2u);
}

RV_TEST(exec_alu, x0_is_hardwired_to_zero) {
    Machine m;
    m.load({
        I(InstrId::ADDI, 0, 0, 42),  // addi x0, x0, 42  -- must be discarded
        EBREAK(),
    });
    m.run();
    RV_CHECK_HEX(m.reg(0), 0u);
}

RV_TEST(exec_alu, addition_wraps_without_trapping) {
    Machine m;
    m.load({
        U(InstrId::LUI, 1, static_cast<rv::i32>(0x8000'0000u)),  // x1 = 0x80000000
        I(InstrId::ADDI, 1, 1, -1),                              // wraps to 0x7fffffff
        EBREAK(),
    });
    m.run();
    RV_CHECK_HEX(m.reg(1), 0x7fff'ffffu);
}

RV_TEST(exec_alu, slt_is_signed_and_sltu_is_not) {
    Machine m;
    m.load({
        I(InstrId::ADDI, 1, 0, -1),  // x1 = 0xffffffff
        I(InstrId::ADDI, 2, 0, 1),   // x2 = 1
        R(InstrId::SLT, 3, 1, 2),    // signed:   -1 < 1  -> 1
        R(InstrId::SLTU, 4, 1, 2),   // unsigned: huge < 1 -> 0
        EBREAK(),
    });
    m.run();
    RV_CHECK_HEX(m.reg(3), 1u);
    RV_CHECK_HEX(m.reg(4), 0u);
}

RV_TEST(exec_alu, sltiu_sign_extends_then_compares_unsigned) {
    // The immediate is sign-extended first and only then treated as unsigned,
    // which is what makes `sltiu rd, rs, 1` the idiomatic "rs == 0" test and
    // `sltiu rd, rs, -1` almost always true.
    Machine m;
    m.load({
        I(InstrId::ADDI, 1, 0, 0),     // x1 = 0
        I(InstrId::SLTIU, 2, 1, 1),    // 0 < 1 -> 1
        I(InstrId::ADDI, 3, 0, 5),     // x3 = 5
        I(InstrId::SLTIU, 4, 3, 1),    // 5 < 1 -> 0
        I(InstrId::SLTIU, 5, 3, -1),   // 5 < 0xffffffff -> 1
        EBREAK(),
    });
    m.run();
    RV_CHECK_HEX(m.reg(2), 1u);
    RV_CHECK_HEX(m.reg(4), 0u);
    RV_CHECK_HEX(m.reg(5), 1u);
}

RV_TEST(exec_alu, logical_operations) {
    Machine m;
    m.load({
        I(InstrId::ADDI, 1, 0, 0x0f0),
        I(InstrId::ADDI, 2, 0, 0x0ff),
        R(InstrId::AND, 3, 1, 2),
        R(InstrId::OR, 4, 1, 2),
        R(InstrId::XOR, 5, 1, 2),
        I(InstrId::XORI, 6, 1, -1),  // xori with -1 is the idiomatic "not"
        EBREAK(),
    });
    m.run();
    RV_CHECK_HEX(m.reg(3), 0x0f0u);
    RV_CHECK_HEX(m.reg(4), 0x0ffu);
    RV_CHECK_HEX(m.reg(5), 0x00fu);
    RV_CHECK_HEX(m.reg(6), 0xffff'ff0fu);
}

RV_TEST(exec_alu, shifts_use_only_the_low_five_bits) {
    // "the shift amount is the low 5 bits of rs2" -- a full 32-bit shift amount
    // would be undefined behaviour in the host language, so this also proves we
    // are masking rather than relying on the hardware's happenstance.
    Machine m;
    m.load({
        I(InstrId::ADDI, 1, 0, 1),    // x1 = 1
        I(InstrId::ADDI, 2, 0, 33),   // x2 = 33, low 5 bits = 1
        R(InstrId::SLL, 3, 1, 2),     // 1 << 1 = 2, not 0
        EBREAK(),
    });
    m.run();
    RV_CHECK_HEX(m.reg(3), 2u);
}

RV_TEST(exec_alu, srl_is_logical_and_sra_is_arithmetic) {
    Machine m;
    m.load({
        I(InstrId::ADDI, 1, 0, -8),      // x1 = 0xfffffff8
        I(InstrId::SRLI, 2, 1, 1),       // logical:    0x7ffffffc
        I(InstrId::SRAI, 3, 1, 1),       // arithmetic: 0xfffffffc
        I(InstrId::SRAI, 4, 1, 31),      // all sign bits
        EBREAK(),
    });
    m.run();
    RV_CHECK_HEX(m.reg(2), 0x7fff'fffcu);
    RV_CHECK_HEX(m.reg(3), 0xffff'fffcu);
    RV_CHECK_HEX(m.reg(4), 0xffff'ffffu);
}

RV_TEST(exec_alu, lui_and_auipc) {
    Machine m;
    m.load({
        U(InstrId::LUI, 1, 0x1234'5000),    // x1 = 0x12345000
        U(InstrId::AUIPC, 2, 0x0000'1000),  // at pc = 4, so x2 = 4 + 0x1000
        EBREAK(),
    });
    m.run();
    RV_CHECK_HEX(m.reg(1), 0x1234'5000u);
    // auipc is relative to the address of the auipc itself, not to pc+4. This
    // off-by-one is the classic auipc bug.
    RV_CHECK_HEX(m.reg(2), 0x0000'1004u);
}
