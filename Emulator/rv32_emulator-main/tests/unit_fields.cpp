// Bit-field extraction and insertion.
//
// The round-trip tests here are the reason a scrambled B-type or J-type
// immediate cannot survive: any bit placed in the wrong position shows up
// immediately as an asymmetry between the encoder and the decoder.
#include <random>

#include "isa/fields.hpp"
#include "rv_test.hpp"

using namespace rv;
using namespace rv::isa;

RV_TEST(fields, sign_extend_boundaries) {
    RV_CHECK_EQ(sign_extend(0x7ff, 12), 2047);
    RV_CHECK_EQ(sign_extend(0x800, 12), -2048);
    RV_CHECK_EQ(sign_extend(0xfff, 12), -1);
    RV_CHECK_EQ(sign_extend(0x000, 12), 0);
    RV_CHECK_EQ(sign_extend(0x7f, 8), 127);
    RV_CHECK_EQ(sign_extend(0x80, 8), -128);
    // Width 32 must be a no-op rather than an undefined shift.
    RV_CHECK_EQ(sign_extend(0xffff'ffffu, 32), -1);
    RV_CHECK_EQ(sign_extend(0x8000'0000u, 32), -2147483647 - 1);
}

RV_TEST(fields, shifts_mask_the_amount) {
    // RISC-V uses only the low 5 bits of a shift amount. In C++ a shift of 32
    // or more is undefined behaviour, so this must be masked, not merely
    // clamped after the fact.
    RV_CHECK_HEX(shift_left(1u, 32), 1u);
    RV_CHECK_HEX(shift_left(1u, 33), 2u);
    RV_CHECK_HEX(shift_right_logical(0x8000'0000u, 32), 0x8000'0000u);
    RV_CHECK_HEX(shift_right_arith(0x8000'0000u, 1), 0xc000'0000u);
    RV_CHECK_HEX(shift_right_arith(0x8000'0000u, 31), 0xffff'ffffu);
    RV_CHECK_HEX(shift_right_arith(0x4000'0000u, 30), 0x0000'0001u);
    // A zero shift must not paste sign bits in.
    RV_CHECK_HEX(shift_right_arith(0x8000'0000u, 0), 0x8000'0000u);
}

RV_TEST(fields, bit_extraction) {
    RV_CHECK_HEX(bits(0xdead'beefu, 31, 28), 0xdu);
    RV_CHECK_HEX(bits(0xdead'beefu, 3, 0), 0xfu);
    RV_CHECK_HEX(bits(0xdead'beefu, 31, 0), 0xdead'beefu);  // full width, no UB
    RV_CHECK_HEX(bit(0x8000'0000u, 31), 1u);
    RV_CHECK_HEX(bit(0x8000'0000u, 30), 0u);
}

RV_TEST(fields, immediate_round_trip) {
    std::mt19937 rng(12345);
    std::uniform_int_distribution<i32> dist(-(1 << 20), (1 << 20) - 1);

    for (int iteration = 0; iteration < 20000; ++iteration) {
        const i32 raw = dist(rng);

        // I-type and S-type: 12-bit signed.
        const i32 imm12 = sign_extend(static_cast<u32>(raw), 12);
        RV_CHECK_EQ(imm_i(put_imm_i(imm12)), imm12);
        RV_CHECK_EQ(imm_s(put_imm_s(imm12)), imm12);

        // B-type: 13-bit signed, always even.
        const i32 imm13 = sign_extend(static_cast<u32>(raw) & ~1u, 13);
        RV_CHECK_EQ(imm_b(put_imm_b(imm13)), imm13);

        // J-type: 21-bit signed, always even.
        const i32 imm21 = sign_extend(static_cast<u32>(raw) & ~1u, 21);
        RV_CHECK_EQ(imm_j(put_imm_j(imm21)), imm21);

        // U-type: the top 20 bits, low 12 always zero.
        const i32 imm_upper = static_cast<i32>(static_cast<u32>(raw) << 12);
        RV_CHECK_EQ(imm_u(put_imm_u(imm_upper)), imm_upper);
    }
}

RV_TEST(fields, immediates_do_not_disturb_other_fields) {
    // Each inserter must contribute only the bits its own format owns, so a
    // caller can OR the pieces of an instruction together in any order.
    // Note that S-type and B-type immediates legitimately occupy [11:7] -- the
    // slot an R-type or I-type would use for rd -- which is exactly why those
    // formats have no rd operand.
    constexpr Word kIFieldMask = 0xfff0'0000u;  // [31:20]
    constexpr Word kSFieldMask = 0xfe00'0f80u;  // [31:25] and [11:7]
    constexpr Word kUFieldMask = 0xffff'f000u;  // [31:12]

    RV_CHECK_HEX(put_imm_i(-1) & ~kIFieldMask, 0u);
    RV_CHECK_HEX(put_imm_s(-1) & ~kSFieldMask, 0u);
    RV_CHECK_HEX(put_imm_b(-2) & ~kSFieldMask, 0u);
    RV_CHECK_HEX(put_imm_j(-2) & ~kUFieldMask, 0u);
    RV_CHECK_HEX(put_imm_u(-4096) & ~kUFieldMask, 0u);
    RV_CHECK_HEX(put_rd(31) & ~0x0000'0f80u, 0u);
    RV_CHECK_HEX(put_rs1(31) & ~0x000f'8000u, 0u);
    RV_CHECK_HEX(put_rs2(31) & ~0x01f0'0000u, 0u);
}

RV_TEST(fields, register_field_positions) {
    // rd=1, rs1=2, rs2=3 in an otherwise empty word.
    const Word word = put_rd(1) | put_rs1(2) | put_rs2(3);
    RV_CHECK_EQ(rd_of(word), 1);
    RV_CHECK_EQ(rs1_of(word), 2);
    RV_CHECK_EQ(rs2_of(word), 3);
}

// ---- compressed (C extension) fields ---------------------------------------

RV_TEST(fields, compressed_register_fields) {
    // The 3-bit fields address x8-x15 only. Zero means x8, not x0 -- getting
    // that bias wrong would silently turn every c.lw into a load from the wrong
    // register rather than into anything that looks like an error.
    RV_CHECK_EQ(creg(0), 8);
    RV_CHECK_EQ(creg(7), 15);

    const Word word = put_crs1p(9) | put_crs2p(14);
    RV_CHECK_EQ(crs1p_of(word), 9);
    RV_CHECK_EQ(crs2p_of(word), 14);

    const Word full = put_crd(31) | put_crs2(17);
    RV_CHECK_EQ(crd_of(full), 31);
    RV_CHECK_EQ(crs2_of(full), 17);
}

RV_TEST(fields, compressed_immediate_round_trip) {
    // Exhaustive rather than sampled: every one of these fields is small enough
    // to enumerate completely, so there is no reason to settle for random
    // coverage of a scatter this easy to get wrong.
    for (i32 imm = -32; imm <= 31; ++imm) {
        RV_CHECK_EQ(c_imm_ci(put_c_imm_ci(imm)), imm);
    }
    for (u32 shamt = 0; shamt < 64; ++shamt) {
        RV_CHECK_HEX(c_shamt(put_c_shamt(shamt)), shamt);
    }
    // c.lui carries its value already positioned at bits 17:12.
    for (i32 raw = -32; raw <= 31; ++raw) {
        const i32 imm = static_cast<i32>(static_cast<u32>(raw) << 12);
        RV_CHECK_EQ(c_imm_lui(put_c_imm_lui(imm)), imm);
    }
    // c.addi16sp: signed, a multiple of 16, -512..496.
    for (i32 imm = -512; imm <= 496; imm += 16) {
        RV_CHECK_EQ(c_imm_addi16sp(put_c_imm_addi16sp(imm)), imm);
    }
    // c.addi4spn: unsigned, a multiple of 4, 0..1020.
    for (u32 imm = 0; imm <= 1020; imm += 4) {
        RV_CHECK_HEX(c_imm_addi4spn(put_c_imm_addi4spn(imm)), imm);
    }
    // Three different layouts for the same quantity -- a word offset.
    for (u32 imm = 0; imm <= 124; imm += 4) {
        RV_CHECK_HEX(c_imm_lw(put_c_imm_lw(imm)), imm);
    }
    for (u32 imm = 0; imm <= 252; imm += 4) {
        RV_CHECK_HEX(c_imm_lwsp(put_c_imm_lwsp(imm)), imm);
        RV_CHECK_HEX(c_imm_swsp(put_c_imm_swsp(imm)), imm);
    }
    // c.j / c.jal: signed, even, +-2 KB.
    for (i32 imm = -2048; imm <= 2046; imm += 2) {
        RV_CHECK_EQ(c_imm_cj(put_c_imm_cj(imm)), imm);
    }
    // c.beqz / c.bnez: signed, even, +-256 B.
    for (i32 imm = -256; imm <= 254; imm += 2) {
        RV_CHECK_EQ(c_imm_cb(put_c_imm_cb(imm)), imm);
    }
}

RV_TEST(fields, compressed_immediates_stay_inside_16_bits) {
    // A compressed inserter that spilled a bit above bit 15 would corrupt the
    // *next* instruction in the halfword stream, which is a far nastier failure
    // than a wrong operand.
    constexpr Word kHalf = 0xffff'0000u;
    RV_CHECK_HEX(put_c_imm_ci(-1) & kHalf, 0u);
    RV_CHECK_HEX(put_c_shamt(63) & kHalf, 0u);
    RV_CHECK_HEX(put_c_imm_lui(-4096) & kHalf, 0u);
    RV_CHECK_HEX(put_c_imm_addi16sp(-16) & kHalf, 0u);
    RV_CHECK_HEX(put_c_imm_addi4spn(1020) & kHalf, 0u);
    RV_CHECK_HEX(put_c_imm_lw(124) & kHalf, 0u);
    RV_CHECK_HEX(put_c_imm_lwsp(252) & kHalf, 0u);
    RV_CHECK_HEX(put_c_imm_swsp(252) & kHalf, 0u);
    RV_CHECK_HEX(put_c_imm_cj(-2) & kHalf, 0u);
    RV_CHECK_HEX(put_c_imm_cb(-2) & kHalf, 0u);
    RV_CHECK_HEX(put_crd(31) & kHalf, 0u);
    RV_CHECK_HEX(put_crs2(31) & kHalf, 0u);

    // The op[1:0] slot belongs to the quadrant, never to an immediate.
    RV_CHECK_HEX(put_c_imm_ci(-1) & 3u, 0u);
    RV_CHECK_HEX(put_c_imm_cj(-2) & 3u, 0u);
    RV_CHECK_HEX(put_c_imm_cb(-2) & 3u, 0u);
    RV_CHECK_HEX(put_c_imm_lwsp(252) & 3u, 0u);
}
