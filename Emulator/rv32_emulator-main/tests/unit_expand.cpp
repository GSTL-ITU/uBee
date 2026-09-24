// Compressed instructions expand to the base instructions they alias.
//
// This is the file that carries the weight for the whole C extension. Because
// src/core/exec.cpp has no compressed cases -- the hart expands first and runs
// the result -- every compressed instruction is only as correct as its entry in
// expand(). A wrong base id or a mis-set rs1 here would sail through the
// round-trip tests, which prove the *bits* are right, and show up as a program
// that quietly computes the wrong answer.
#include "isa/decode.hpp"
#include "isa/encode.hpp"
#include "isa/expand.hpp"
#include "rv_test.hpp"
#include "sample_operands.hpp"

using namespace rv;
using namespace rv::isa;

namespace {

/// Decode a halfword and expand it, in one step.
DecodedInstr expanded(Word half) { return expand(decode(half)); }

}  // namespace

RV_TEST(expand, every_compressed_row_expands_to_a_base_instruction) {
    // A switch cannot be made exhaustive over just the C_* part of InstrId, so
    // the "did you forget one?" guarantee lives here instead: walk the table,
    // and require every compressed row to come back as something else.
    int seen = 0;
    for (std::size_t i = 0; i < kInstrCount; ++i) {
        const auto id = static_cast<InstrId>(i);
        const InstrDesc& desc = describe(id);
        if (!is_compressed_format(desc.format)) continue;
        ++seen;

        const EncodeResult encoded = encode(id, sample::operands_for(id));
        RV_CHECK(encoded.ok());
        if (!encoded.ok()) continue;

        const DecodedInstr in = decode(encoded.word);
        RV_CHECK_EQ(mnemonic_of(in.id), desc.mnemonic);

        const DecodedInstr out = expand(in);
        RV_CHECK(out.valid());
        // Expanded to something, and that something is not compressed.
        RV_CHECK(!is_compressed_format(out.desc().format));
        RV_CHECK_NE(static_cast<int>(out.id), static_cast<int>(in.id));
        // The width is carried across: the pc still advances by 2.
        RV_CHECK_EQ(static_cast<int>(out.length), 2);
        // mtval wants the halfword that actually faulted, not the expansion.
        RV_CHECK_HEX(out.raw, in.raw);
    }
    RV_CHECK_EQ(seen, 27);
}

RV_TEST(expand, a_base_instruction_expands_to_itself) {
    // expand() is called unconditionally by the hart, so it has to be the
    // identity on everything that is already a base instruction.
    for (std::size_t i = 0; i < kInstrCount; ++i) {
        const auto id = static_cast<InstrId>(i);
        if (is_compressed_format(describe(id).format)) continue;

        const EncodeResult encoded = encode(id, sample::operands_for(id));
        if (!encoded.ok()) continue;
        const DecodedInstr in = decode(encoded.word);
        const DecodedInstr out = expand(in);
        RV_CHECK_EQ(static_cast<int>(out.id), static_cast<int>(in.id));
        RV_CHECK_HEX(out.raw, in.raw);
        RV_CHECK_EQ(static_cast<int>(out.length), 4);
    }

    // Including an invalid one, which must not be mistaken for a compressed
    // row and rewritten into something that runs.
    RV_CHECK(!expand(decode(0xffff'ffffu)).valid());
}

RV_TEST(expand, the_read_modify_write_forms_reuse_rd_as_rs1) {
    // `c.addi a0, 1` is `addi a0, a0, 1`. If rs1 were left at zero it would be
    // `addi a0, x0, 1`, which is c.li -- a plausible-looking result that is
    // wrong for every value of a0 except zero.
    const DecodedInstr addi = expanded(0x0505);  // c.addi a0, 1
    RV_CHECK_EQ(static_cast<int>(addi.id), static_cast<int>(InstrId::ADDI));
    RV_CHECK_EQ(addi.rd, 10);
    RV_CHECK_EQ(addi.rs1, 10);
    RV_CHECK_EQ(addi.imm, 1);

    const DecodedInstr add = expanded(0x952e);  // c.add a0, a1
    RV_CHECK_EQ(static_cast<int>(add.id), static_cast<int>(InstrId::ADD));
    RV_CHECK_EQ(add.rd, 10);
    RV_CHECK_EQ(add.rs1, 10);
    RV_CHECK_EQ(add.rs2, 11);
}

RV_TEST(expand, the_load_immediate_forms_source_from_zero) {
    // The counterpart to the above, and the reason the two must not share a
    // case: c.li writes a constant, so rs1 has to be x0 and not rd.
    const DecodedInstr li = expanded(0x4505);  // c.li a0, 1
    RV_CHECK_EQ(static_cast<int>(li.id), static_cast<int>(InstrId::ADDI));
    RV_CHECK_EQ(li.rd, 10);
    RV_CHECK_EQ(li.rs1, 0);
    RV_CHECK_EQ(li.imm, 1);

    // c.mv must expand to `add rd, x0, rs2`, not to an addi -- an addi would
    // add the register *number* rather than copying the register.
    const DecodedInstr mv = expanded(0x852e);  // c.mv a0, a1
    RV_CHECK_EQ(static_cast<int>(mv.id), static_cast<int>(InstrId::ADD));
    RV_CHECK_EQ(mv.rd, 10);
    RV_CHECK_EQ(mv.rs1, 0);
    RV_CHECK_EQ(mv.rs2, 11);
}

RV_TEST(expand, c_nop_is_the_canonical_nop) {
    const DecodedInstr nop = expanded(0x0001);
    RV_CHECK_EQ(static_cast<int>(nop.id), static_cast<int>(InstrId::ADDI));
    RV_CHECK_EQ(nop.rd, 0);
    RV_CHECK_EQ(nop.rs1, 0);
    RV_CHECK_EQ(nop.imm, 0);
}

RV_TEST(expand, the_jumps_supply_the_right_link_register) {
    // c.j and c.jr discard the link; c.jal and c.jalr write ra. Confusing the
    // pairs would turn a call into a jump, and the return would go wherever ra
    // happened to point.
    const DecodedInstr j = expanded(0xa001);  // c.j 0
    RV_CHECK_EQ(static_cast<int>(j.id), static_cast<int>(InstrId::JAL));
    RV_CHECK_EQ(j.rd, 0);

    const DecodedInstr jal = expanded(0x2001);  // c.jal 0
    RV_CHECK_EQ(static_cast<int>(jal.id), static_cast<int>(InstrId::JAL));
    RV_CHECK_EQ(jal.rd, 1);

    const DecodedInstr jr = expanded(0x8582);  // c.jr a1
    RV_CHECK_EQ(static_cast<int>(jr.id), static_cast<int>(InstrId::JALR));
    RV_CHECK_EQ(jr.rd, 0);
    RV_CHECK_EQ(jr.rs1, 11);
    RV_CHECK_EQ(jr.imm, 0);

    const DecodedInstr jalr = expanded(0x9582);  // c.jalr a1
    RV_CHECK_EQ(static_cast<int>(jalr.id), static_cast<int>(InstrId::JALR));
    RV_CHECK_EQ(jalr.rd, 1);
    RV_CHECK_EQ(jalr.rs1, 11);
    RV_CHECK_EQ(jalr.imm, 0);
}

RV_TEST(expand, the_branches_compare_against_zero) {
    const DecodedInstr beqz = expanded(0xc111);  // c.beqz a0, ...
    RV_CHECK_EQ(static_cast<int>(beqz.id), static_cast<int>(InstrId::BEQ));
    RV_CHECK_EQ(beqz.rs1, 10);
    RV_CHECK_EQ(beqz.rs2, 0);

    const DecodedInstr bnez = expanded(0xe111);  // c.bnez a0, ...
    RV_CHECK_EQ(static_cast<int>(bnez.id), static_cast<int>(InstrId::BNE));
    RV_CHECK_EQ(bnez.rs1, 10);
    RV_CHECK_EQ(bnez.rs2, 0);
}

RV_TEST(expand, the_stack_pointer_forms_address_through_x2) {
    // c.lwsp, c.swsp, c.addi4spn and c.addi16sp all have sp baked into the
    // encoding rather than written down, so the expansion has to supply it.
    const DecodedInstr lwsp = expanded(0x4512);  // c.lwsp a0, 4(sp)
    RV_CHECK_EQ(static_cast<int>(lwsp.id), static_cast<int>(InstrId::LW));
    RV_CHECK_EQ(lwsp.rd, 10);
    RV_CHECK_EQ(lwsp.rs1, 2);

    const DecodedInstr swsp = expanded(0xc22a);  // c.swsp a0, 4(sp)
    RV_CHECK_EQ(static_cast<int>(swsp.id), static_cast<int>(InstrId::SW));
    RV_CHECK_EQ(swsp.rs2, 10);
    RV_CHECK_EQ(swsp.rs1, 2);

    const DecodedInstr spn = expanded(0x0040);  // c.addi4spn s0, 4
    RV_CHECK_EQ(static_cast<int>(spn.id), static_cast<int>(InstrId::ADDI));
    RV_CHECK_EQ(spn.rs1, 2);
    RV_CHECK_EQ(spn.imm, 4);

    const DecodedInstr sp16 = expanded(0x6105);  // c.addi16sp 32
    RV_CHECK_EQ(static_cast<int>(sp16.id), static_cast<int>(InstrId::ADDI));
    RV_CHECK_EQ(sp16.rd, 2);
    RV_CHECK_EQ(sp16.rs1, 2);
    RV_CHECK_EQ(sp16.imm, 32);
}

RV_TEST(expand, the_shifts_keep_their_direction) {
    // Three rows that differ only in two bits and mean three different things.
    RV_CHECK_EQ(static_cast<int>(expanded(0x0506).id), static_cast<int>(InstrId::SLLI));
    RV_CHECK_EQ(static_cast<int>(expanded(0x8105).id), static_cast<int>(InstrId::SRLI));
    RV_CHECK_EQ(static_cast<int>(expanded(0x8505).id), static_cast<int>(InstrId::SRAI));
    RV_CHECK_EQ(static_cast<int>(expanded(0x8905).id), static_cast<int>(InstrId::ANDI));
}

RV_TEST(expand, the_register_register_forms_keep_their_operation) {
    RV_CHECK_EQ(static_cast<int>(expanded(0x8c05).id), static_cast<int>(InstrId::SUB));
    RV_CHECK_EQ(static_cast<int>(expanded(0x8c25).id), static_cast<int>(InstrId::XOR));
    RV_CHECK_EQ(static_cast<int>(expanded(0x8c45).id), static_cast<int>(InstrId::OR));
    RV_CHECK_EQ(static_cast<int>(expanded(0x8c65).id), static_cast<int>(InstrId::AND));

    // and that they read and write the same register in the rd' slot.
    const DecodedInstr sub = expanded(0x8c05);  // c.sub s0, s1
    RV_CHECK_EQ(sub.rd, 8);
    RV_CHECK_EQ(sub.rs1, 8);
    RV_CHECK_EQ(sub.rs2, 9);
}

RV_TEST(expand, c_ebreak_is_a_real_ebreak) {
    // It has to reach the same halt path as the 32-bit ebreak, or a compressed
    // program could never stop.
    RV_CHECK_EQ(static_cast<int>(expanded(0x9002).id), static_cast<int>(InstrId::EBREAK));
}

RV_TEST(expand, c_lui_keeps_its_immediate_positioned) {
    // Like lui, the value is already at bits 17:12; expanding must not shift it
    // again.
    const DecodedInstr lui = expanded(0x6505);  // c.lui a0, 1
    RV_CHECK_EQ(static_cast<int>(lui.id), static_cast<int>(InstrId::LUI));
    RV_CHECK_EQ(lui.rd, 10);
    RV_CHECK_HEX(static_cast<u32>(lui.imm), 0x1000u);
}
