// The M extension: multiply, divide and remainder.
//
// Every edge case below is mandated by the spec and is easy to get wrong. The
// division cases matter twice over: computing INT32_MIN / -1 with the host's
// native operator is undefined behaviour in C++, so the implementation has to
// special-case it rather than compute and then correct.
#include <limits>

#include "machine_fixture.hpp"

using namespace fixture;
using rv::isa::InstrId;

namespace {

/// Run `op rd, x1, x2` with the given inputs and return rd.
rv::u32 binop(InstrId op, rv::u32 lhs, rv::u32 rhs) {
    Machine m;
    m.load({R(op, 3, 1, 2), EBREAK()});
    m.set_reg(1, lhs);
    m.set_reg(2, rhs);
    m.run();
    return m.reg(3);
}

constexpr rv::u32 kIntMin = 0x8000'0000u;

}  // namespace

RV_TEST(exec_m, mul_returns_the_low_word) {
    RV_CHECK_HEX(binop(InstrId::MUL, 6, 7), 42u);
    RV_CHECK_HEX(binop(InstrId::MUL, 0xffff'ffffu, 0xffff'ffffu), 1u);  // (-1) * (-1)
    // Overflow is discarded silently, not trapped.
    RV_CHECK_HEX(binop(InstrId::MUL, 0x1000'0000u, 0x10u), 0u);
}

RV_TEST(exec_m, mulh_variants_differ_in_how_operands_are_extended) {
    // -1 * -1 = 1, whose high word is 0.
    RV_CHECK_HEX(binop(InstrId::MULH, 0xffff'ffffu, 0xffff'ffffu), 0u);
    // As unsigned, 0xffffffff * 0xffffffff = 0xfffffffe00000001.
    RV_CHECK_HEX(binop(InstrId::MULHU, 0xffff'ffffu, 0xffff'ffffu), 0xffff'fffeu);
    // Signed x unsigned: (-1) * 0xffffffff = -4294967295, high word 0xffffffff.
    RV_CHECK_HEX(binop(InstrId::MULHSU, 0xffff'ffffu, 0xffff'ffffu), 0xffff'ffffu);

    // A case where all three genuinely disagree, so a copy-paste between them
    // cannot pass.
    RV_CHECK_HEX(binop(InstrId::MULH, kIntMin, 2), 0xffff'ffffu);
    RV_CHECK_HEX(binop(InstrId::MULHU, kIntMin, 2), 0x0000'0001u);
    RV_CHECK_HEX(binop(InstrId::MULHSU, kIntMin, 2), 0xffff'ffffu);
}

RV_TEST(exec_m, division_by_zero_is_defined_and_does_not_trap) {
    // "The quotient of division by zero has all bits set."
    RV_CHECK_HEX(binop(InstrId::DIV, 17, 0), 0xffff'ffffu);
    RV_CHECK_HEX(binop(InstrId::DIVU, 17, 0), 0xffff'ffffu);
    // "The remainder of division by zero equals the dividend."
    RV_CHECK_HEX(binop(InstrId::REM, 17, 0), 17u);
    RV_CHECK_HEX(binop(InstrId::REMU, 17, 0), 17u);
    RV_CHECK_HEX(binop(InstrId::REM, 0xffff'fff0u, 0), 0xffff'fff0u);
}

RV_TEST(exec_m, signed_division_overflow_is_defined) {
    // INT32_MIN / -1 overflows. The spec fixes the results rather than
    // trapping: the quotient is INT32_MIN and the remainder is zero.
    RV_CHECK_HEX(binop(InstrId::DIV, kIntMin, 0xffff'ffffu), kIntMin);
    RV_CHECK_HEX(binop(InstrId::REM, kIntMin, 0xffff'ffffu), 0u);
}

RV_TEST(exec_m, division_truncates_toward_zero) {
    // Not floor division: -7 / 2 is -3, and the remainder takes the sign of the
    // dividend.
    RV_CHECK_HEX(binop(InstrId::DIV, static_cast<rv::u32>(-7), 2), static_cast<rv::u32>(-3));
    RV_CHECK_HEX(binop(InstrId::REM, static_cast<rv::u32>(-7), 2), static_cast<rv::u32>(-1));
    RV_CHECK_HEX(binop(InstrId::DIV, 7, static_cast<rv::u32>(-2)), static_cast<rv::u32>(-3));
    RV_CHECK_HEX(binop(InstrId::REM, 7, static_cast<rv::u32>(-2)), 1u);
}

RV_TEST(exec_m, unsigned_division_treats_operands_as_unsigned) {
    // 0xffffffff / 2 is about 2^31 unsigned, but -1 / 2 is 0 signed.
    RV_CHECK_HEX(binop(InstrId::DIVU, 0xffff'ffffu, 2), 0x7fff'ffffu);
    RV_CHECK_HEX(binop(InstrId::DIV, 0xffff'ffffu, 2), 0u);
    RV_CHECK_HEX(binop(InstrId::REMU, 0xffff'ffffu, 2), 1u);
}
