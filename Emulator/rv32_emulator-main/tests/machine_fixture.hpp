// A small machine builder so that executor tests read like assembly.
//
//   Machine m;
//   m.load({I(ADDI, 1, 0, 5), I(ADDI, 2, 0, 3), R(ADD, 3, 1, 2), EBREAK()});
//   m.run();
//   RV_CHECK_HEX(m.reg(3), 8);
//
// Once the assembler exists (M3) most of these move to .s files under
// tests/asm/, but the encoding-level fixture stays useful for the cases that
// are hard or impossible to write in assembly -- illegal instructions,
// deliberately misaligned targets, reserved funct7 values.
#pragma once

#include <initializer_list>
#include <vector>

#include "core/hart.hpp"
#include "isa/encode.hpp"
#include "rv_test.hpp"

namespace fixture {

using rv::Addr;
using rv::i32;
using rv::RegIdx;
using rv::u32;
using rv::Word;
using rv::isa::InstrId;

/// Encode or die: a test that mis-specifies an operand should fail loudly at
/// the point of construction rather than silently assemble to something else.
inline Word enc(InstrId id, const rv::isa::Operands& operands) {
    const rv::isa::EncodeResult result = rv::isa::encode(id, operands);
    if (!result.ok()) {
        std::fprintf(stderr, "fixture: cannot encode %s: %s (%lld)\n",
                     std::string(rv::isa::mnemonic_of(id)).c_str(),
                     rv::isa::encode_error_message(result.error),
                     static_cast<long long>(result.value));
        std::abort();
    }
    return result.word;
}

inline Word R(InstrId id, RegIdx rd, RegIdx rs1, RegIdx rs2) {
    return enc(id, {rd, rs1, rs2, 0, 0});
}
inline Word I(InstrId id, RegIdx rd, RegIdx rs1, i32 imm) { return enc(id, {rd, rs1, 0, imm, 0}); }
inline Word S(InstrId id, RegIdx rs1, RegIdx rs2, i32 imm) { return enc(id, {0, rs1, rs2, imm, 0}); }
inline Word B(InstrId id, RegIdx rs1, RegIdx rs2, i32 imm) { return enc(id, {0, rs1, rs2, imm, 0}); }
inline Word U(InstrId id, RegIdx rd, i32 imm) { return enc(id, {rd, 0, 0, imm, 0}); }
inline Word J(InstrId id, RegIdx rd, i32 imm) { return enc(id, {rd, 0, 0, imm, 0}); }
inline Word CSR(InstrId id, RegIdx rd, rv::CsrAddr csr, RegIdx rs1) {
    return enc(id, {rd, rs1, 0, 0, csr});
}
inline Word CSRI(InstrId id, RegIdx rd, rv::CsrAddr csr, i32 zimm) {
    return enc(id, {rd, 0, 0, zimm, csr});
}
inline Word EBREAK() { return enc(InstrId::EBREAK, {}); }
inline Word ECALL() { return enc(InstrId::ECALL, {}); }

class Machine {
public:
    Machine() { hart_.reset(0); }

    void load(std::initializer_list<Word> words) { load(std::vector<Word>(words)); }

    /// For tests that build their program with a helper rather than a literal.
    void load(const std::vector<Word>& words) {
        hart_.imem().load_words(words);
        hart_.reset(0);
    }

    rv::core::StepOutcome step() { return hart_.step(); }

    /// Run until halt or until the budget is exhausted. Returns the number of
    /// instructions executed, so a test can assert a loop ran the expected
    /// number of times.
    int run(int budget = 10000) {
        int count = 0;
        while (!hart_.halted() && count < budget) {
            hart_.step();
            ++count;
        }
        return count;
    }

    u32 reg(RegIdx index) const { return hart_.cpu().x[index]; }
    void set_reg(RegIdx index, u32 value) { hart_.cpu().set_reg(index, value); }
    Addr pc() const { return hart_.cpu().pc; }

    u32 dmem_word(Addr addr) const { return hart_.bus().dmem().read_word_raw(addr); }
    void set_dmem_word(Addr addr, u32 value) { hart_.bus().dmem().write_word_raw(addr, value); }

    rv::core::Hart& hart() { return hart_; }
    const rv::core::Hart& hart() const { return hart_; }

private:
    rv::core::Hart hart_;
};

}  // namespace fixture
