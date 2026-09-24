#include "decode.hpp"

#include <algorithm>
#include <array>
#include <vector>

#include "fields.hpp"

namespace rv::isa {
namespace {

/// A full-width instruction has bits [1:0] == 0b11, so those two bits carry no
/// information and bucketing on opcode[6:2] gives 32 buckets holding at most a
/// handful of rows each.
constexpr std::size_t kBucketCount = 32;

/// The compressed encodings use the three op[1:0] values the base encoding
/// leaves free. Bucketing on (quadrant, funct3) gives 24 buckets; the CR and CA
/// rows share one, which the mask-width sort below then orders correctly.
constexpr std::size_t kCBucketCount = 24;

using Buckets = std::array<std::vector<InstrId>, kBucketCount>;
using CBuckets = std::array<std::vector<InstrId>, kCBucketCount>;

int popcount(Word m) {
    int n = 0;
    for (; m != 0; m &= m - 1) ++n;
    return n;
}

/// Try the most specific rows first. `ecall` (mask 0xffffffff) and `slli`
/// (mask 0xfe00707f) must be tested before any row whose mask is a subset of
/// theirs, otherwise a broad row would swallow a narrow one. This matters far
/// more once the compressed rows exist: `c.nop` sits inside `c.addi`, `c.ebreak`
/// inside `c.jalr` inside `c.add`, and `c.addi16sp` inside `c.lui`. Sorting
/// makes the precedence a property of the decoder rather than of the order rows
/// happen to be listed in.
void sort_by_specificity(std::vector<InstrId>& bucket) {
    std::sort(bucket.begin(), bucket.end(), [](InstrId a, InstrId b) {
        return popcount(describe(a).mask) > popcount(describe(b).mask);
    });
}

std::size_t compressed_bucket(Word half) {
    return static_cast<std::size_t>(bits(half, 1, 0)) * 8u + bits(half, 15, 13);
}

Buckets build_buckets() {
    Buckets buckets;
    for (std::size_t i = 0; i < kInstrCount; ++i) {
        const InstrDesc& desc = kInstrTable[i];
        if (is_compressed_format(desc.format)) continue;
        buckets[bits(desc.match, 6, 2)].push_back(static_cast<InstrId>(i));
    }
    for (auto& bucket : buckets) sort_by_specificity(bucket);
    return buckets;
}

CBuckets build_compressed_buckets() {
    CBuckets buckets;
    for (std::size_t i = 0; i < kInstrCount; ++i) {
        const InstrDesc& desc = kInstrTable[i];
        if (!is_compressed_format(desc.format)) continue;
        buckets[compressed_bucket(desc.match)].push_back(static_cast<InstrId>(i));
    }
    for (auto& bucket : buckets) sort_by_specificity(bucket);
    return buckets;
}

const Buckets& buckets() {
    static const Buckets table = build_buckets();
    return table;
}

const CBuckets& compressed_buckets() {
    static const CBuckets table = build_compressed_buckets();
    return table;
}

/// The reserved compressed encodings, which a match/mask cannot express.
///
/// Each of these is "some field must not be zero", and a mask can only say
/// "these bits are fixed". They are genuinely per-instruction facts, so they
/// are written out per instruction rather than bent into an extra table column
/// that would only ever have five non-empty entries.
///
/// Getting this wrong is not cosmetic: `0x0000` is c.addi4spn with nzuimm == 0,
/// and it has to stay illegal or running off the end of a program into zeroed
/// memory would quietly execute instead of halting.
bool is_reserved(InstrId id, Word half) {
    switch (id) {
        case InstrId::C_ADDI4SPN:
            return c_imm_addi4spn(half) == 0;
        case InstrId::C_ADDI16SP:
            return c_imm_addi16sp(half) == 0;
        case InstrId::C_LUI:
            // rd == 0 has no defined meaning, and rd == 2 is c.addi16sp, which
            // has already won the bucket sort by the time we get here.
            return c_imm_lui(half) == 0 || crd_of(half) == 0;
        case InstrId::C_JR:
        case InstrId::C_JALR:
            return crd_of(half) == 0;
        case InstrId::C_LWSP:
            // Unlike c.addi and c.mv, whose rd == x0 forms are harmless HINTs,
            // c.lwsp with rd == x0 is reserved outright.
            return crd_of(half) == 0;
        default:
            return false;
    }
}

/// Pull every operand a syntax uses out of the encoded word. Shared by both
/// widths: a base row never carries a C_* syntax and a compressed row never
/// carries anything else, so one switch covers both without ambiguity.
void fill_operands(DecodedInstr& out, Word w, OperandSyntax syntax) {
    constexpr RegIdx kSp = 2;

    switch (syntax) {
        case OperandSyntax::NONE:
            break;
        case OperandSyntax::RD_RS1_RS2:
            out.rd = rd_of(w);
            out.rs1 = rs1_of(w);
            out.rs2 = rs2_of(w);
            break;
        case OperandSyntax::RD_RS1_IMM:
        case OperandSyntax::RD_OFS_RS1:
            out.rd = rd_of(w);
            out.rs1 = rs1_of(w);
            out.imm = imm_i(w);
            break;
        case OperandSyntax::RD_RS1_SHAMT:
            out.rd = rd_of(w);
            out.rs1 = rs1_of(w);
            out.imm = static_cast<i32>(shamt_of(w));
            break;
        case OperandSyntax::RS2_OFS_RS1:
            out.rs1 = rs1_of(w);
            out.rs2 = rs2_of(w);
            out.imm = imm_s(w);
            break;
        case OperandSyntax::RS1_RS2_LBL:
            out.rs1 = rs1_of(w);
            out.rs2 = rs2_of(w);
            out.imm = imm_b(w);
            break;
        case OperandSyntax::RD_UIMM:
            out.rd = rd_of(w);
            out.imm = imm_u(w);
            break;
        case OperandSyntax::RD_LBL:
            out.rd = rd_of(w);
            out.imm = imm_j(w);
            break;
        case OperandSyntax::RD_CSR_RS1:
            out.rd = rd_of(w);
            out.rs1 = rs1_of(w);
            out.csr = csr_of(w);
            break;
        case OperandSyntax::RD_CSR_ZIMM:
            out.rd = rd_of(w);
            out.rs1 = rs1_of(w);
            out.csr = csr_of(w);
            out.imm = static_cast<i32>(zimm_of(w));  // zero-extended, never signed
            break;

        // ---- compressed ----------------------------------------------------
        // rd and rs1 are filled with the *same* register for the forms that
        // read and write one register, because that is what they mean: `c.addi
        // a0, 1` is `addi a0, a0, 1`. Doing it here rather than in expand()
        // keeps the encoding panel honest about which field is which.
        case OperandSyntax::C_RD_IMM:
            out.rd = crd_of(w);
            out.rs1 = out.rd;
            out.imm = c_imm_ci(w);
            break;
        case OperandSyntax::C_RD_UIMM:
            out.rd = crd_of(w);
            out.imm = c_imm_lui(w);
            break;
        case OperandSyntax::C_RD_SHAMT:
            out.rd = crd_of(w);
            out.rs1 = out.rd;
            out.imm = static_cast<i32>(c_shamt(w));
            break;
        case OperandSyntax::C_RD_RS2:
            out.rd = crd_of(w);
            out.rs2 = crs2_of(w);
            break;
        case OperandSyntax::C_RDP_IMM:
            out.rd = crs1p_of(w);
            out.rs1 = out.rd;
            out.imm = c_imm_ci(w);
            break;
        case OperandSyntax::C_RDP_UIMM:
            out.rd = crs2p_of(w);  // CIW puts rd' at [4:2]
            out.rs1 = kSp;
            out.imm = static_cast<i32>(c_imm_addi4spn(w));
            break;
        case OperandSyntax::C_RDP_SHAMT:
            out.rd = crs1p_of(w);
            out.rs1 = out.rd;
            out.imm = static_cast<i32>(c_shamt(w));
            break;
        case OperandSyntax::C_RDP_RS2P:
            out.rd = crs1p_of(w);
            out.rs1 = out.rd;
            out.rs2 = crs2p_of(w);
            break;
        case OperandSyntax::C_RS1:
            out.rs1 = crd_of(w);  // the rs1 field sits in the rd slot
            break;
        case OperandSyntax::C_IMM:
            out.rd = kSp;
            out.rs1 = kSp;
            out.imm = c_imm_addi16sp(w);
            break;
        case OperandSyntax::C_LBL:
            out.imm = c_imm_cj(w);
            break;
        case OperandSyntax::C_RS1P_LBL:
            out.rs1 = crs1p_of(w);
            out.imm = c_imm_cb(w);
            break;
        case OperandSyntax::C_RDP_OFS_RS1P:
            out.rd = crs2p_of(w);
            out.rs1 = crs1p_of(w);
            out.imm = static_cast<i32>(c_imm_lw(w));
            break;
        case OperandSyntax::C_RS2P_OFS_RS1P:
            out.rs1 = crs1p_of(w);
            out.rs2 = crs2p_of(w);
            out.imm = static_cast<i32>(c_imm_lw(w));
            break;
        case OperandSyntax::C_RD_OFS_SP:
            out.rd = crd_of(w);
            out.rs1 = kSp;
            out.imm = static_cast<i32>(c_imm_lwsp(w));
            break;
        case OperandSyntax::C_RS2_OFS_SP:
            out.rs1 = kSp;
            out.rs2 = crs2_of(w);
            out.imm = static_cast<i32>(c_imm_swsp(w));
            break;
    }
}

DecodedInstr decode_compressed(Word half) {
    DecodedInstr out;
    out.raw = half;
    out.length = 2;

    for (const InstrId candidate : compressed_buckets()[compressed_bucket(half)]) {
        const InstrDesc& desc = describe(candidate);
        if ((half & desc.mask) != desc.match) continue;
        if (is_reserved(candidate, half)) break;  // a reserved encoding, not another row
        out.id = candidate;
        fill_operands(out, half, desc.syntax);
        return out;
    }

    return out;  // id stays kInvalidInstr
}

}  // namespace

u8 instruction_length(Word word) { return (word & 3u) == 3u ? 4 : 2; }

DecodedInstr decode(Word word) {
    if (instruction_length(word) == 2) return decode_compressed(word & 0xffffu);

    DecodedInstr out;
    out.raw = word;
    out.length = 4;

    for (const InstrId candidate : buckets()[bits(word, 6, 2)]) {
        const InstrDesc& desc = describe(candidate);
        if ((word & desc.mask) != desc.match) continue;
        out.id = candidate;
        fill_operands(out, word, desc.syntax);
        return out;
    }

    return out;  // id stays kInvalidInstr
}

}  // namespace rv::isa
