// Every instruction in the table actually executes, and the quiet ones are
// checked against the loud ones they are easy to confuse with.
//
// The round-trip tests prove the encoder and decoder agree; they say nothing
// about what an instruction *does*. -Werror=switch forces the executor to have
// a case for every row, but not to have the right body in it -- writing sltiu's
// semantics under slti would compile, round-trip, and pass everything else.
// This file closes that gap.
#include "machine_fixture.hpp"
#include "sample_operands.hpp"

using namespace fixture;
using rv::isa::InstrId;
using rv::isa::kInstrCount;
using rv::isa::OperandSyntax;

namespace {

/// Build a legal instance of `id` with operands chosen to be harmless: a rd
/// nothing else reads, sources that are zero, and offsets that stay in range.
/// The compressed rows accept none of those, so the choices live in
/// sample_operands.hpp where the other table-walking tests share them.
rv::Word synthesize(InstrId id) { return enc(id, sample::operands_for(id)); }

}  // namespace

RV_TEST(exec_coverage, every_instruction_in_the_table_executes) {
    for (std::size_t index = 0; index < kInstrCount; ++index) {
        const auto id = static_cast<InstrId>(index);
        const rv::Word word = synthesize(id);

        Machine m;
        m.load({word, EBREAK()});
        const rv::core::StepOutcome outcome = m.step();

        if (outcome.trapped) {
            std::printf("  %s trapped: %s\n", std::string(rv::isa::mnemonic_of(id)).c_str(),
                        rv::core::trap_cause_name(outcome.cause));
        }
        // ecall and ebreak are supposed to stop; nothing else should.
        const bool expected_to_stop = id == InstrId::ECALL || id == InstrId::EBREAK;
        RV_CHECK(expected_to_stop || !outcome.trapped);
        RV_CHECK(outcome.instr.valid());
    }
}

// ---- the pairs that are easy to swap --------------------------------------

RV_TEST(exec_coverage, slti_is_signed_where_sltiu_is_not) {
    Machine m;
    m.load({
        I(InstrId::ADDI, 1, 0, -1),   // x1 = 0xffffffff
        I(InstrId::SLTI, 2, 1, 1),    // signed:   -1 < 1  -> 1
        I(InstrId::SLTIU, 3, 1, 1),   // unsigned: huge < 1 -> 0
        EBREAK(),
    });
    m.run();
    RV_CHECK_HEX(m.reg(2), 1u);
    RV_CHECK_HEX(m.reg(3), 0u);
}

RV_TEST(exec_coverage, slli_shifts_left_where_srli_shifts_right) {
    Machine m;
    m.load({
        I(InstrId::ADDI, 1, 0, 0x10),
        I(InstrId::SLLI, 2, 1, 2),   // 0x40
        I(InstrId::SRLI, 3, 1, 2),   // 0x04
        EBREAK(),
    });
    m.run();
    RV_CHECK_HEX(m.reg(2), 0x40u);
    RV_CHECK_HEX(m.reg(3), 0x04u);
}

RV_TEST(exec_coverage, slli_masks_its_shift_amount_to_five_bits) {
    Machine m;
    m.load({
        I(InstrId::ADDI, 1, 0, 1),
        I(InstrId::SLLI, 2, 1, 31),
        EBREAK(),
    });
    m.run();
    RV_CHECK_HEX(m.reg(2), 0x8000'0000u);
}

RV_TEST(exec_coverage, andi_and_ori_are_not_each_other) {
    Machine m;
    m.load({
        I(InstrId::ADDI, 1, 0, 0x0f0),
        I(InstrId::ANDI, 2, 1, 0x0ff),   // 0x0f0
        I(InstrId::ORI, 3, 1, 0x00f),    // 0x0ff
        I(InstrId::XORI, 4, 1, 0x0ff),   // 0x00f
        EBREAK(),
    });
    m.run();
    RV_CHECK_HEX(m.reg(2), 0x0f0u);
    RV_CHECK_HEX(m.reg(3), 0x0ffu);
    RV_CHECK_HEX(m.reg(4), 0x00fu);
}

RV_TEST(exec_coverage, andi_sign_extends_its_immediate) {
    // `andi rd, rs, -1` must be a no-op, not a mask with 0xfff.
    Machine m;
    m.load({
        U(InstrId::LUI, 1, static_cast<rv::i32>(0xabcd'0000u)),
        I(InstrId::ANDI, 2, 1, -1),
        EBREAK(),
    });
    m.run();
    RV_CHECK_HEX(m.reg(2), 0xabcd'0000u);
}

RV_TEST(exec_coverage, bgeu_is_unsigned_where_bge_is_signed) {
    Machine m;
    m.load({
        I(InstrId::ADDI, 1, 0, -1),      // 0xffffffff
        I(InstrId::ADDI, 2, 0, 1),
        B(InstrId::BGEU, 1, 2, 8),       // unsigned: huge >= 1, taken
        I(InstrId::ADDI, 3, 0, 99),      // skipped
        B(InstrId::BGE, 1, 2, 8),        // signed: -1 >= 1 is false, not taken
        I(InstrId::ADDI, 4, 0, 99),      // executed
        EBREAK(),
    });
    m.run();
    RV_CHECK_HEX(m.reg(3), 0u);
    RV_CHECK_HEX(m.reg(4), 99u);
}

RV_TEST(exec_coverage, sh_writes_two_bytes_and_no_more) {
    Machine m;
    m.load({
        I(InstrId::ADDI, 1, 0, 0x40),
        U(InstrId::LUI, 2, static_cast<rv::i32>(0x1234'0000u)),
        I(InstrId::ADDI, 2, 2, 0x567),   // 0x12340567
        S(InstrId::SH, 1, 2, 0),         // only the low halfword
        EBREAK(),
    });
    m.set_dmem_word(0x40, 0xaaaa'aaaau);
    m.run();
    RV_CHECK_HEX(m.dmem_word(0x40), 0xaaaa'0567u);
}

RV_TEST(exec_coverage, sh_to_an_odd_address_traps) {
    Machine m;
    m.load({
        I(InstrId::ADDI, 1, 0, 0x41),
        S(InstrId::SH, 1, 0, 0),
        EBREAK(),
    });
    m.step();
    const rv::core::StepOutcome outcome = m.step();
    RV_CHECK(outcome.trapped);
    RV_CHECK_EQ(static_cast<int>(outcome.cause),
                static_cast<int>(rv::core::TrapCause::StoreAddressMisaligned));
}

RV_TEST(exec_coverage, csrrci_clears_the_bits_its_immediate_names) {
    Machine m;
    m.load({
        CSRI(InstrId::CSRRWI, 0, 0x340, 0x1f),  // mscratch = 31
        CSRI(InstrId::CSRRCI, 1, 0x340, 0x0f),  // clear the low four bits
        CSR(InstrId::CSRRS, 2, 0x340, 0),
        EBREAK(),
    });
    m.run();
    RV_CHECK_HEX(m.reg(1), 31u);   // rd gets the value from before the clear
    RV_CHECK_HEX(m.reg(2), 0x10u);
}

RV_TEST(exec_coverage, csrrci_with_a_zero_immediate_does_not_write) {
    Machine m;
    m.load({
        CSRI(InstrId::CSRRWI, 0, 0x340, 0x15),
        CSRI(InstrId::CSRRCI, 1, 0x340, 0),  // a pure read
        CSR(InstrId::CSRRS, 2, 0x340, 0),
        EBREAK(),
    });
    m.run();
    RV_CHECK_HEX(m.reg(1), 0x15u);
    RV_CHECK_HEX(m.reg(2), 0x15u);
}

RV_TEST(exec_coverage, fence_retires_as_a_no_op) {
    // One hart, in order, no store buffer: architecturally nothing to do. But
    // it must still retire rather than trapping, so code written against a real
    // toolchain runs unchanged.
    Machine m;
    m.load({
        I(InstrId::ADDI, 1, 0, 7),
        enc(InstrId::FENCE, {}),
        I(InstrId::ADDI, 2, 0, 9),
        EBREAK(),
    });
    m.run();
    RV_CHECK_HEX(m.reg(1), 7u);
    RV_CHECK_HEX(m.reg(2), 9u);
    RV_CHECK_EQ(static_cast<int>(m.hart().halt_reason()),
                static_cast<int>(rv::core::HaltReason::Ebreak));
}

RV_TEST(exec_coverage, fence_i_retires_as_a_no_op) {
    // Zifencei. A no-op for a stronger reason than FENCE: this machine is
    // Harvard, so a program cannot write the instruction stream it fetches
    // from and there is never a stale fetch to flush. It still has to retire,
    // because startup code written for a real toolchain contains one.
    Machine m;
    m.load({
        I(InstrId::ADDI, 1, 0, 7),
        enc(InstrId::FENCEI, {}),
        I(InstrId::ADDI, 2, 0, 9),
        EBREAK(),
    });
    m.run();
    RV_CHECK_HEX(m.reg(1), 7u);
    RV_CHECK_HEX(m.reg(2), 9u);
    RV_CHECK_EQ(static_cast<int>(m.hart().halt_reason()),
                static_cast<int>(rv::core::HaltReason::Ebreak));
}

RV_TEST(exec_coverage, fence_and_fence_i_are_distinct_encodings) {
    // They differ only in funct3, and FENCE's mask covers it. A decoder that
    // ignored funct3 here would silently accept one as the other.
    RV_CHECK_HEX(enc(InstrId::FENCE, {}), 0x0000000fu);
    RV_CHECK_HEX(enc(InstrId::FENCEI, {}), 0x0000100fu);
    RV_CHECK_EQ(static_cast<int>(rv::isa::decode(0x0000100f).id),
                static_cast<int>(InstrId::FENCEI));
}
