// Instruction decoding: an instruction word in, a DecodedInstr out.
//
// This lives in rv_isa rather than rv_core because both siblings need it: the
// executor decodes to run, and the disassembler decodes to print.
#pragma once

#include "isa.hpp"
#include "types.hpp"

namespace rv::isa {

/// A decoded instruction with every field already extracted and sign-extended.
///
/// Which fields are meaningful depends on `desc().syntax`; the rest are zero.
/// For RD_CSR_ZIMM (`csrrwi` and friends) `imm` holds the zero-extended 5-bit
/// immediate, and `rs1` holds the same bits raw — the encoding panel shows the
/// field in its rs1 slot, while the executor wants it as a value.
struct DecodedInstr {
    /// The encoded instruction. For a compressed one this is the 16-bit
    /// halfword zero-extended, not the word it was fetched from -- so a caller
    /// printing it should use `length` to decide how many digits to show.
    Word raw = 0;
    InstrId id = kInvalidInstr;
    /// Encoded width in bytes, 2 or 4. Valid even when the decode failed, so
    /// the fetch can still tell how far to advance past a bad halfword.
    u8 length = 4;
    RegIdx rd = 0;
    RegIdx rs1 = 0;
    RegIdx rs2 = 0;
    i32 imm = 0;
    CsrAddr csr = 0;

    bool valid() const { return id != kInvalidInstr; }
    const InstrDesc& desc() const { return describe(id); }
};

/// How wide the instruction starting with these bits is: 2 unless the low two
/// bits are both set. Only the low 16 bits are examined, so a fetch can ask
/// before it knows whether the upper half is even readable.
u8 instruction_length(Word word);

/// Decode a word. A word whose low two bits are not 0b11 is decoded as a
/// compressed instruction from its low halfword, and the upper half is ignored.
///
/// An unrecognised encoding yields `id == kInvalidInstr` with `raw` and
/// `length` still populated, so the caller can raise IllegalInstruction with
/// the offending bits in mtval and still know how wide they were.
DecodedInstr decode(Word word);

}  // namespace rv::isa
