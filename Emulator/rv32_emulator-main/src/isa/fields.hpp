// Bit-field layout of a RISC-V instruction word, 32-bit and compressed.
//
// Every immediate has an extractor and a matching inserter, and
// unit_fields.cpp round-trips each pair over random values. That round-trip is
// the only thing standing between us and a scrambled B-type immediate, which is
// the single most common bug in a hand-written RISC-V implementation (bit 11
// and bit 12 swap places relative to every other format).
#pragma once

#include "types.hpp"

namespace rv::isa {

// ---- register and opcode fields -------------------------------------------

constexpr u32 opcode_of(Word w) { return bits(w, 6, 0); }
constexpr RegIdx rd_of(Word w) { return static_cast<RegIdx>(bits(w, 11, 7)); }
constexpr u32 funct3_of(Word w) { return bits(w, 14, 12); }
constexpr RegIdx rs1_of(Word w) { return static_cast<RegIdx>(bits(w, 19, 15)); }
constexpr RegIdx rs2_of(Word w) { return static_cast<RegIdx>(bits(w, 24, 20)); }
constexpr u32 funct7_of(Word w) { return bits(w, 31, 25); }

/// Shift amount for slli/srli/srai: the low 5 bits of the I-immediate field.
constexpr u32 shamt_of(Word w) { return bits(w, 24, 20); }
/// CSR address for the Zicsr instructions: the whole I-immediate field.
constexpr CsrAddr csr_of(Word w) { return static_cast<CsrAddr>(bits(w, 31, 20)); }
/// 5-bit *zero*-extended immediate for csrrwi/csrrsi/csrrci, in the rs1 slot.
constexpr u32 zimm_of(Word w) { return bits(w, 19, 15); }

// ---- immediate extraction --------------------------------------------------

constexpr i32 imm_i(Word w) { return sign_extend(bits(w, 31, 20), 12); }

constexpr i32 imm_s(Word w) {
    return sign_extend((bits(w, 31, 25) << 5) | bits(w, 11, 7), 12);
}

constexpr i32 imm_b(Word w) {
    // imm[12|10:5] live in [31:25], imm[4:1|11] in [11:7], and imm[0] is
    // implicitly zero (branch targets are 2-byte aligned).
    const u32 value = (bit(w, 31) << 12) | (bit(w, 7) << 11) | (bits(w, 30, 25) << 5) |
                      (bits(w, 11, 8) << 1);
    return sign_extend(value, 13);
}

constexpr i32 imm_u(Word w) {
    // Already positioned at bits 31:12; no shift and no sign extension.
    return static_cast<i32>(w & 0xffff'f000u);
}

constexpr i32 imm_j(Word w) {
    const u32 value = (bit(w, 31) << 20) | (bits(w, 19, 12) << 12) | (bit(w, 20) << 11) |
                      (bits(w, 30, 21) << 1);
    return sign_extend(value, 21);
}

// ---- immediate insertion ---------------------------------------------------
// Each inserter returns only its own bits, to be OR-ed into a word alongside
// the opcode and register fields. Range checking is the caller's job (the
// assembler reports it as a diagnostic pointing at the operand).

constexpr Word put_rd(RegIdx r) { return static_cast<Word>(r & 31u) << 7; }
constexpr Word put_rs1(RegIdx r) { return static_cast<Word>(r & 31u) << 15; }
constexpr Word put_rs2(RegIdx r) { return static_cast<Word>(r & 31u) << 20; }
constexpr Word put_shamt(u32 amount) { return (amount & 31u) << 20; }
constexpr Word put_csr(CsrAddr csr) { return (static_cast<Word>(csr) & 0xfffu) << 20; }
constexpr Word put_zimm(u32 value) { return (value & 31u) << 15; }

constexpr Word put_imm_i(i32 imm) {
    return (static_cast<Word>(imm) & 0xfffu) << 20;
}

constexpr Word put_imm_s(i32 imm) {
    const Word v = static_cast<Word>(imm);
    return (bits(v, 11, 5) << 25) | (bits(v, 4, 0) << 7);
}

constexpr Word put_imm_b(i32 imm) {
    const Word v = static_cast<Word>(imm);
    return (bit(v, 12) << 31) | (bits(v, 10, 5) << 25) | (bits(v, 4, 1) << 8) | (bit(v, 11) << 7);
}

constexpr Word put_imm_u(i32 imm) { return static_cast<Word>(imm) & 0xffff'f000u; }

constexpr Word put_imm_j(i32 imm) {
    const Word v = static_cast<Word>(imm);
    return (bit(v, 20) << 31) | (bits(v, 10, 1) << 21) | (bit(v, 11) << 20) |
           (bits(v, 19, 12) << 12);
}

// ---- compressed (C extension) fields ---------------------------------------
//
// If the B-type immediate is the classic RISC-V footgun, these are nine of
// them. Every one of these immediates is scattered, most are scaled, and no two
// scatter the same way -- C.LW, C.LWSP and C.SWSP all encode a word offset and
// all three put the bits somewhere different. So each extractor gets a matching
// inserter and unit_fields.cpp round-trips every pair, which is the only thing
// that makes these safe to write down once.
//
// Bit numbering below is the spec's: `nzuimm[5:4]` at `inst[12:11]` means bits
// 12 and 11 of the halfword hold bits 5 and 4 of the immediate.

/// The 3-bit register fields select from x8-x15 only -- the eight registers a
/// compiler keeps hot. That is the whole trick that buys the encoding space.
constexpr RegIdx creg(u32 field) { return static_cast<RegIdx>((field & 7u) + 8u); }
/// rd'/rs1' at inst[9:7], the CL/CS/CA/CB position.
constexpr RegIdx crs1p_of(Word w) { return creg(bits(w, 9, 7)); }
/// rd'/rs2' at inst[4:2], the CIW/CL/CS/CA position.
constexpr RegIdx crs2p_of(Word w) { return creg(bits(w, 4, 2)); }
/// Full 5-bit rd/rs1 at inst[11:7] (CR, CI).
constexpr RegIdx crd_of(Word w) { return static_cast<RegIdx>(bits(w, 11, 7)); }
/// Full 5-bit rs2 at inst[6:2] (CR, CSS).
constexpr RegIdx crs2_of(Word w) { return static_cast<RegIdx>(bits(w, 6, 2)); }

constexpr Word put_crs1p(RegIdx r) { return (static_cast<Word>(r) & 7u) << 7; }
constexpr Word put_crs2p(RegIdx r) { return (static_cast<Word>(r) & 7u) << 2; }
constexpr Word put_crd(RegIdx r) { return (static_cast<Word>(r) & 31u) << 7; }
constexpr Word put_crs2(RegIdx r) { return (static_cast<Word>(r) & 31u) << 2; }

/// CI signed 6-bit: imm[5] at inst[12], imm[4:0] at inst[6:2].
/// Used by c.addi, c.li, c.andi.
constexpr i32 c_imm_ci(Word w) { return sign_extend((bit(w, 12) << 5) | bits(w, 6, 2), 6); }
constexpr Word put_c_imm_ci(i32 imm) {
    const Word v = static_cast<Word>(imm);
    return (bit(v, 5) << 12) | (bits(v, 4, 0) << 2);
}

/// CI shift amount, unsigned 6-bit in the same slots. RV32 requires bit 5 to be
/// zero, which the table's mask enforces, so the value is really 0..31.
constexpr u32 c_shamt(Word w) { return (bit(w, 12) << 5) | bits(w, 6, 2); }
constexpr Word put_c_shamt(u32 shamt) {
    return (bit(shamt, 5) << 12) | (bits(shamt, 4, 0) << 2);
}

/// C.LUI: nzimm[17] at inst[12], nzimm[16:12] at inst[6:2]. Returns the value
/// already positioned at bits 17:12, matching how imm_u treats lui.
constexpr i32 c_imm_lui(Word w) {
    return sign_extend(((bit(w, 12) << 5) | bits(w, 6, 2)) << 12, 18);
}
constexpr Word put_c_imm_lui(i32 imm) {
    const Word v = static_cast<Word>(imm);
    return (bit(v, 17) << 12) | (bits(v, 16, 12) << 2);
}

/// C.ADDI16SP: nzimm[9] at inst[12], then inst[6]=nzimm[4], inst[5]=nzimm[6],
/// inst[4:3]=nzimm[8:7], inst[2]=nzimm[5]. Signed, a multiple of 16.
constexpr i32 c_imm_addi16sp(Word w) {
    const u32 value = (bit(w, 12) << 9) | (bits(w, 4, 3) << 7) | (bit(w, 5) << 6) |
                      (bit(w, 2) << 5) | (bit(w, 6) << 4);
    return sign_extend(value, 10);
}
constexpr Word put_c_imm_addi16sp(i32 imm) {
    const Word v = static_cast<Word>(imm);
    return (bit(v, 9) << 12) | (bits(v, 8, 7) << 3) | (bit(v, 6) << 5) | (bit(v, 5) << 2) |
           (bit(v, 4) << 6);
}

/// C.ADDI4SPN: nzuimm[5:4] at inst[12:11], nzuimm[9:6] at inst[10:7],
/// nzuimm[2] at inst[6], nzuimm[3] at inst[5]. Unsigned, a multiple of 4.
constexpr u32 c_imm_addi4spn(Word w) {
    return (bits(w, 10, 7) << 6) | (bits(w, 12, 11) << 4) | (bit(w, 5) << 3) | (bit(w, 6) << 2);
}
constexpr Word put_c_imm_addi4spn(u32 imm) {
    return (bits(imm, 9, 6) << 7) | (bits(imm, 5, 4) << 11) | (bit(imm, 3) << 5) |
           (bit(imm, 2) << 6);
}

/// C.LW / C.SW: uimm[5:3] at inst[12:10], uimm[2] at inst[6], uimm[6] at
/// inst[5]. Unsigned, a multiple of 4, 0..124.
constexpr u32 c_imm_lw(Word w) {
    return (bit(w, 5) << 6) | (bits(w, 12, 10) << 3) | (bit(w, 6) << 2);
}
constexpr Word put_c_imm_lw(u32 imm) {
    return (bit(imm, 6) << 5) | (bits(imm, 5, 3) << 10) | (bit(imm, 2) << 6);
}

/// C.LWSP: uimm[5] at inst[12], uimm[4:2] at inst[6:4], uimm[7:6] at inst[3:2].
/// Unsigned, a multiple of 4, 0..252. Note this is *not* the C.LW layout.
constexpr u32 c_imm_lwsp(Word w) {
    return (bits(w, 3, 2) << 6) | (bit(w, 12) << 5) | (bits(w, 6, 4) << 2);
}
constexpr Word put_c_imm_lwsp(u32 imm) {
    return (bits(imm, 7, 6) << 2) | (bit(imm, 5) << 12) | (bits(imm, 4, 2) << 4);
}

/// C.SWSP: uimm[5:2] at inst[12:9], uimm[7:6] at inst[8:7]. A third layout for
/// the same quantity again.
constexpr u32 c_imm_swsp(Word w) {
    return (bits(w, 8, 7) << 6) | (bits(w, 12, 9) << 2);
}
constexpr Word put_c_imm_swsp(u32 imm) {
    return (bits(imm, 7, 6) << 7) | (bits(imm, 5, 2) << 9);
}

/// CJ: offset[11] at inst[12], [4] at [11], [9:8] at [10:9], [10] at [8],
/// [6] at [7], [7] at [6], [3:1] at [5:3], [5] at [2]. Signed, even, +-2 KB.
constexpr i32 c_imm_cj(Word w) {
    const u32 value = (bit(w, 12) << 11) | (bit(w, 8) << 10) | (bits(w, 10, 9) << 8) |
                      (bit(w, 6) << 7) | (bit(w, 7) << 6) | (bit(w, 2) << 5) |
                      (bit(w, 11) << 4) | (bits(w, 5, 3) << 1);
    return sign_extend(value, 12);
}
constexpr Word put_c_imm_cj(i32 imm) {
    const Word v = static_cast<Word>(imm);
    return (bit(v, 11) << 12) | (bit(v, 10) << 8) | (bits(v, 9, 8) << 9) | (bit(v, 7) << 6) |
           (bit(v, 6) << 7) | (bit(v, 5) << 2) | (bit(v, 4) << 11) | (bits(v, 3, 1) << 3);
}

/// CB branch: offset[8] at inst[12], [4:3] at [11:10], [7:6] at [6:5],
/// [2:1] at [4:3], [5] at [2]. Signed, even, +-256 B.
constexpr i32 c_imm_cb(Word w) {
    const u32 value = (bit(w, 12) << 8) | (bits(w, 6, 5) << 6) | (bit(w, 2) << 5) |
                      (bits(w, 11, 10) << 3) | (bits(w, 4, 3) << 1);
    return sign_extend(value, 9);
}
constexpr Word put_c_imm_cb(i32 imm) {
    const Word v = static_cast<Word>(imm);
    return (bit(v, 8) << 12) | (bits(v, 7, 6) << 5) | (bit(v, 5) << 2) | (bits(v, 4, 3) << 10) |
           (bits(v, 2, 1) << 3);
}

}  // namespace rv::isa
