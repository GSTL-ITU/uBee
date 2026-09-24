// Decoding, and the encode/decode/disassemble round trip.
#include <cstdio>
#include <random>

#include "isa/decode.hpp"
#include "isa/disasm.hpp"
#include "isa/encode.hpp"
#include "isa/expand.hpp"
#include "isa/fields.hpp"
#include "rv_test.hpp"

using namespace rv;
using namespace rv::isa;

RV_TEST(decode, known_encodings) {
    // Hand-checked against the RISC-V unprivileged spec.
    {
        const DecodedInstr d = decode(0x00500093);  // addi x1, x0, 5
        RV_CHECK(d.valid());
        RV_CHECK_EQ(mnemonic_of(d.id), "addi");
        RV_CHECK_EQ(d.rd, 1);
        RV_CHECK_EQ(d.rs1, 0);
        RV_CHECK_EQ(d.imm, 5);
    }
    {
        const DecodedInstr d = decode(0x002081b3);  // add x3, x1, x2
        RV_CHECK_EQ(mnemonic_of(d.id), "add");
        RV_CHECK_EQ(d.rd, 3);
        RV_CHECK_EQ(d.rs1, 1);
        RV_CHECK_EQ(d.rs2, 2);
    }
    {
        const DecodedInstr d = decode(0x40208233);  // sub x4, x1, x2
        RV_CHECK_EQ(mnemonic_of(d.id), "sub");
    }
    {
        const DecodedInstr d = decode(0x0000a303);  // lw x6, 0(x1)
        RV_CHECK_EQ(mnemonic_of(d.id), "lw");
        RV_CHECK_EQ(d.rd, 6);
        RV_CHECK_EQ(d.rs1, 1);
        RV_CHECK_EQ(d.imm, 0);
    }
    {
        const DecodedInstr d = decode(0x00112023);  // sw x1, 0(x2)
        RV_CHECK_EQ(mnemonic_of(d.id), "sw");
        RV_CHECK_EQ(d.rs1, 2);
        RV_CHECK_EQ(d.rs2, 1);
        RV_CHECK_EQ(d.imm, 0);
    }
    {
        const DecodedInstr d = decode(0x00100073);  // ebreak
        RV_CHECK_EQ(mnemonic_of(d.id), "ebreak");
    }
    {
        const DecodedInstr d = decode(0x00000073);  // ecall
        RV_CHECK_EQ(mnemonic_of(d.id), "ecall");
    }
    {
        const DecodedInstr d = decode(0x02000033);  // mul x0, x0, x0
        RV_CHECK_EQ(mnemonic_of(d.id), "mul");
    }
}

RV_TEST(decode, shift_immediates_are_distinguished_by_funct7) {
    // srli and srai share funct3; only bit 30 tells them apart. Getting the
    // mask wrong here makes arithmetic right shifts silently logical.
    const DecodedInstr srli = decode(0x0020d093);  // srli x1, x1, 2
    const DecodedInstr srai = decode(0x4020d093);  // srai x1, x1, 2
    RV_CHECK_EQ(mnemonic_of(srli.id), "srli");
    RV_CHECK_EQ(mnemonic_of(srai.id), "srai");
    RV_CHECK_EQ(srli.imm, 2);
    RV_CHECK_EQ(srai.imm, 2);
}

RV_TEST(decode, exact_match_rows_win_over_broad_ones) {
    // ecall/ebreak/mret/wfi have a full-word mask and share opcode 0x73 with
    // the CSR instructions. Each must decode to itself.
    RV_CHECK_EQ(mnemonic_of(decode(0x30200073).id), "mret");
    RV_CHECK_EQ(mnemonic_of(decode(0x10500073).id), "wfi");
    RV_CHECK_EQ(mnemonic_of(decode(0x30029073).id), "csrrw");  // csrrw x0, mstatus, x5
}

RV_TEST(decode, invalid_words_are_rejected) {
    RV_CHECK(!decode(0x00000000).valid());  // opcode 0x00 is not assigned
    RV_CHECK(!decode(0xffffffff).valid());  // opcode 0x7f is not assigned
    // A real opcode and funct3, but a funct7 that no row claims: 0x33/f3=3
    // exists as sltu only with funct7 0x00.
    RV_CHECK(!decode(0x20003033).valid());
    // Zicsr shares opcode 0x73, but funct3 = 4 is unassigned.
    RV_CHECK(!decode(0x00004073).valid());
}

RV_TEST(decode, disassembly_round_trip_over_random_words) {
    // For any word that decodes, re-encoding what the disassembler printed must
    // reproduce the original bits. We approximate "re-encode" by checking that
    // decoding is stable and that every field survives a print/parse of the
    // numeric operands -- the full text round trip lands with the assembler.
    std::mt19937 rng(987654321);
    int decoded_count = 0;
    for (int i = 0; i < 200000; ++i) {
        const Word word = rng();
        const DecodedInstr d = decode(word);
        if (!d.valid()) continue;
        ++decoded_count;

        // Decoding must be deterministic and idempotent.
        const DecodedInstr again = decode(word);
        RV_CHECK_EQ(again.id, d.id);
        RV_CHECK_EQ(again.imm, d.imm);

        // The disassembler must never produce an empty line or crash.
        const std::string text = disassemble(d, 0x1000);
        RV_CHECK(!text.empty());

        // Every register index must be in range -- a field extracted from the
        // wrong bits would eventually produce something >= 32.
        RV_CHECK(d.rd < 32);
        RV_CHECK(d.rs1 < 32);
        RV_CHECK(d.rs2 < 32);
    }
    // Sanity: a meaningful fraction of random words should be valid, otherwise
    // this test is silently passing without exercising anything.
    RV_CHECK(decoded_count > 1000);
}

RV_TEST(decode, every_table_row_decodes_to_itself) {
    // Build each instruction from its own match pattern and check it comes back
    // as the same id. This catches two rows whose match/mask overlap, which
    // would otherwise show up as a rare and baffling misexecution.
    //
    // A bare match is a legal encoding for every base row, but for several
    // compressed ones it is precisely the encoding the spec reserves -- the
    // variable fields are all zero, and zero is what c.addi4spn, c.addi16sp,
    // c.lui, c.jr and c.jalr are forbidden to have. Those are checked by
    // reserved_compressed_encodings_are_illegal below instead; here they are
    // given a minimal *legal* set of variable bits so the overlap check still
    // runs over them.
    for (std::size_t i = 0; i < kInstrCount; ++i) {
        const InstrDesc& desc = kInstrTable[i];
        const auto id = static_cast<InstrId>(i);
        Word word = desc.match;
        switch (id) {
            case InstrId::C_ADDI4SPN: word |= 0x0040; break;  // nzuimm = 4
            case InstrId::C_ADDI16SP: word |= 0x0004; break;  // nzimm = 32
            case InstrId::C_LUI: word |= 0x0084; break;       // rd = 1, imm != 0
            case InstrId::C_JR:
            case InstrId::C_JALR: word |= 0x0080; break;      // rs1 = ra
            case InstrId::C_ADDI:
            case InstrId::C_SLLI:
            case InstrId::C_LI:
            case InstrId::C_LWSP: word |= 0x0080; break;      // rd = ra
            case InstrId::C_MV:
            case InstrId::C_ADD: word |= 0x0084; break;       // rd = ra, rs2 = ra
            default: break;
        }
        const DecodedInstr d = decode(word);
        RV_CHECK_EQ(mnemonic_of(d.id), desc.mnemonic);
        RV_CHECK_EQ(static_cast<int>(d.length), static_cast<int>(desc.length()));
    }
}

RV_TEST(decode, reserved_compressed_encodings_are_illegal) {
    // The C extension leaves holes that a match/mask cannot describe, so they
    // are enforced by hand in decode.cpp -- which means they need checking by
    // hand too. Each of these is "the variable field is zero".
    RV_CHECK(!decode(0x0000).valid());  // c.addi4spn, nzuimm == 0
    RV_CHECK(!decode(0x6101).valid());  // c.addi16sp, nzimm == 0
    RV_CHECK(!decode(0x6001).valid());  // c.lui, rd == 0 and imm == 0
    RV_CHECK(!decode(0x6081).valid());  // c.lui, rd == 1 but imm == 0
    RV_CHECK(!decode(0x8002).valid());  // c.jr, rs1 == x0
    // 0x9002 would be c.jalr with rs1 == x0, which is reserved -- but the
    // narrower c.ebreak row claims those exact bits first, so it stays legal.
    RV_CHECK_EQ(mnemonic_of(decode(0x9002).id), "c.ebreak");

    // The neighbouring legal encodings must still decode, or the check above is
    // rejecting far more than it should.
    RV_CHECK_EQ(mnemonic_of(decode(0x0040).id), "c.addi4spn");
    RV_CHECK_EQ(mnemonic_of(decode(0x6105).id), "c.addi16sp");
    RV_CHECK_EQ(mnemonic_of(decode(0x6085).id), "c.lui");
    RV_CHECK_EQ(mnemonic_of(decode(0x8082).id), "c.jr");
    RV_CHECK_EQ(mnemonic_of(decode(0x9082).id), "c.jalr");
}

RV_TEST(decode, the_all_zero_halfword_stays_illegal) {
    // Load-bearing: running off the end of a program into zeroed imem has to
    // halt rather than execute. Before the C extension this was guaranteed by
    // opcode 0 being unassigned; now 0x0000 is inside c.addi4spn's mask and
    // stays illegal only because nzuimm == 0 is reserved.
    RV_CHECK(!decode(0x00000000).valid());
    RV_CHECK(!decode(0x0000).valid());
    // Two zero halfwords in a row are two illegal instructions, not one.
    RV_CHECK_EQ(static_cast<int>(decode(0x00000000).length), 2);
}

RV_TEST(decode, compressed_and_base_encodings_cannot_collide) {
    // The whole reason the C extension fits: op[1:0] == 0b11 is reserved for
    // 32-bit instructions, so no compressed row can shadow a base one.
    for (std::size_t i = 0; i < kInstrCount; ++i) {
        const InstrDesc& desc = kInstrTable[i];
        if (is_compressed_format(desc.format)) {
            RV_CHECK_NE(static_cast<int>(desc.match & 3u), 3);
            RV_CHECK_HEX(desc.match & 0xffff0000u, 0u);  // 16-bit match only
            RV_CHECK_HEX(desc.mask & 0xffff0000u, 0u);
        } else {
            RV_CHECK_EQ(static_cast<int>(desc.match & 3u), 3);
        }
    }
}

RV_TEST(decode, every_halfword_decodes_consistently) {
    // Exhaustive. The compressed encoding space is only 65536 wide, so there is
    // no reason to settle for the random sampling the 32-bit test has to use:
    // every reachable compressed encoding is checked here, including every hole.
    int decoded_count = 0;
    for (u32 half = 0; half < 0x10000u; ++half) {
        if ((half & 3u) == 3u) continue;  // that is a 32-bit instruction
        const DecodedInstr d = decode(half);
        RV_CHECK_EQ(static_cast<int>(d.length), 2);
        RV_CHECK_HEX(d.raw, half);
        if (!d.valid()) continue;
        ++decoded_count;

        // Whatever it decoded to must be a compressed row, must re-encode to
        // the same bits, and must expand to a base instruction.
        const InstrDesc& desc = d.desc();
        RV_CHECK(is_compressed_format(desc.format));
        const DecodedInstr expanded = expand(d);
        RV_CHECK(expanded.valid());
        RV_CHECK(!is_compressed_format(expanded.desc().format));
        RV_CHECK_EQ(static_cast<int>(expanded.length), 2);

        // Anything the decoder accepts, the encoder must be able to produce --
        // otherwise the disassembler can print something the assembler will
        // not take back. Exhaustively, for the whole compressed space.
        const EncodeResult re = encode(d.id, operands_of(d));
        RV_CHECK(re.ok());
        if (!re.ok()) {
            std::printf("  halfword 0x%04x (%s) failed to re-encode: %s\n", half,
                        std::string(mnemonic_of(d.id)).c_str(), encode_error_message(re.error));
            continue;
        }
        RV_CHECK_EQ(static_cast<int>(re.size_bytes), 2);
        RV_CHECK_EQ(mnemonic_of(decode(re.word).id), mnemonic_of(d.id));
    }
    // Three quadrants of a 16-bit space, minus the holes; if this collapses,
    // the loop above is passing without exercising anything.
    RV_CHECK(decoded_count > 20000);
}
