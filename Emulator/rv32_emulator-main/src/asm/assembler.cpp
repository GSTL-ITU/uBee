#include "asm/assembler.hpp"

#include <algorithm>
#include <cstdio>

#include "asm/diag_render.hpp"
#include "asm/parser.hpp"
#include "core/memfile.hpp"
#include "isa/csr.hpp"
#include "isa/encode.hpp"
#include "isa/expand.hpp"
#include "isa/regnames.hpp"

namespace rv::as {
namespace {

using isa::InstrId;

/// One output section. Both text and data are byte buffers with an absolute
/// cursor; bytes are stored relative to `origin`, so `.byte` inside `.text`, a
/// backwards `.org`, and a non-zero IMEM/DMEM base (uBee SoC) all work.
struct Section {
    std::vector<u8> bytes;
    Addr origin = 0;
    Addr cursor = 0;

    std::size_t index_of(Addr addr) const { return static_cast<std::size_t>(addr - origin); }

    void reserve_to(std::size_t end) {
        if (bytes.size() < end) bytes.resize(end, 0);
    }
    void emit_byte(u8 value) {
        const std::size_t index = index_of(cursor);
        reserve_to(index + 1);
        bytes[index] = value;
        ++cursor;
    }
    void emit_half(u16 value) {
        emit_byte(static_cast<u8>(value));
        emit_byte(static_cast<u8>(value >> 8));
    }
    void emit_word(Word value) {
        emit_half(static_cast<u16>(value));
        emit_half(static_cast<u16>(value >> 16));
    }
    void skip(u32 count) {
        const std::size_t index = index_of(cursor);
        reserve_to(index + count);
        cursor += count;
    }
    void rewind_to_origin() { cursor = origin; }
};

struct Value {
    i64 number = 0;
    Space space = Space::Absolute;
    bool ok = false;
};

/// Split a 32-bit value into the lui/addi pair. The +0x800 bias compensates
/// for the low half being sign-extended by addi: without it, any value whose
/// bit 11 is set would come out 0x1000 too small.
struct HiLo {
    i32 hi;  // the 20-bit operand for lui
    i32 lo;  // the 12-bit signed operand for addi
};

HiLo split_hi_lo(i64 value) {
    const u32 bits32 = static_cast<u32>(value);
    const i32 lo = sign_extend(bits32 & 0xfffu, 12);
    const i32 hi = static_cast<i32>((bits32 - static_cast<u32>(lo)) >> 12) & 0xfffff;
    return HiLo{hi, lo};
}

class Assembler {
public:
    Assembler(const SourceFile& source, const AssembleOptions& options, AssembledProgram& out)
        : source_(source), options_(options), out_(out) {}

    void run();

private:
    // ---- diagnostics ------------------------------------------------------
    void error(std::string code, std::string message, SourceSpan span, std::string label = {},
               std::string hint = {}) {
        out_.diagnostics.error(std::move(code), std::move(message), span, std::move(label),
                               std::move(hint));
    }
    void warn(std::string code, std::string message, SourceSpan span, std::string label = {},
              std::string hint = {}) {
        out_.diagnostics.warning(std::move(code), std::move(message), span, std::move(label),
                                 std::move(hint));
    }

    // ---- passes -----------------------------------------------------------
    void pass_layout();
    void pass_encode();

    Section& section() { return space_ == Space::Data ? data_ : text_; }
    Addr cursor() const { return space_ == Space::Data ? data_.cursor : text_.cursor; }

    // ---- expression evaluation --------------------------------------------
    Value eval(int node, bool report);
    Value eval_operand(const AsmLine& line, std::size_t index, bool report);

    // ---- operand accessors ------------------------------------------------
    bool expect_operand_count(const AsmLine& line, std::size_t count);
    bool want_register(const AsmLine& line, std::size_t index, RegIdx& out);
    bool want_value(const AsmLine& line, std::size_t index, i64& out, Space& space);
    bool want_memory(const AsmLine& line, std::size_t index, RegIdx& base, i64& offset);
    bool want_csr(const AsmLine& line, std::size_t index, CsrAddr& out);
    bool want_compressible_register(const AsmLine& line, std::size_t index, RegIdx& out);
    bool want_nonzero_register(const AsmLine& line, std::size_t index, RegIdx& out);
    /// A branch or jump target: must resolve into the text space.
    bool want_code_target(const AsmLine& line, std::size_t index, Addr& target);

    // ---- emission ---------------------------------------------------------
    void emit(InstrId id, const isa::Operands& operands, SourceSpan span,
              SourceSpan value_span = SourceSpan{});
    void begin_line(const AsmLine& line);
    void encode_instruction(const AsmLine& line);
    void encode_pseudo(const AsmLine& line);
    void run_directive_layout(const AsmLine& line, AsmLine& mutable_line);
    void run_directive_encode(const AsmLine& line);

    const SourceFile& source_;
    const AssembleOptions& options_;
    AssembledProgram& out_;

    ParsedProgram program_;
    Section text_;
    Section data_;
    Space space_ = Space::Text;

    // Emission bookkeeping for the current source line.
    u32 current_line_ = 0;
    PseudoId current_pseudo_ = kInvalidPseudo;
    u8 current_slot_ = 0;
    u8 current_count_ = 1;
    Addr current_line_addr_ = 0;
    bool explained_la_sizing_ = false;
};

// ---------------------------------------------------------------------------
// Expression evaluation
// ---------------------------------------------------------------------------

Value Assembler::eval(int node, bool report) {
    Value result;
    if (!program_.exprs.valid(node)) return result;
    const Expr& expr = program_.exprs[node];

    switch (expr.kind) {
        case Expr::Kind::Number:
            result.number = expr.value;
            result.space = Space::Absolute;
            result.ok = true;
            return result;

        case Expr::Kind::Dot:
            result.number = current_line_addr_;
            result.space = space_;
            result.ok = true;
            return result;

        case Expr::Kind::Symbol: {
            if (const Symbol* symbol = out_.symbols.find(expr.name)) {
                result.number = symbol->value;
                result.space = symbol->space;
                result.ok = true;
                return result;
            }
            // Not a program symbol: a CSR name is the other thing an
            // identifier can legitimately be. Checking the symbol table first
            // means `.equ mstatus, 5` still shadows it, which is the least
            // surprising order.
            if (const isa::CsrDesc* csr = isa::find_csr_by_name(expr.name)) {
                result.number = csr->addr;
                result.space = Space::Absolute;
                result.ok = true;
                return result;
            }
            if (report) {
                // `x33` is not a symbol and never will be, but it is a very
                // common thing to type. Say what is actually wrong rather than
                // making the reader work it out from "undefined symbol".
                if (expr.name.size() >= 2 && expr.name[0] == 'x' &&
                    std::all_of(expr.name.begin() + 1, expr.name.end(),
                                [](char c) { return c >= '0' && c <= '9'; })) {
                    error("E0203", "'" + std::string(expr.name) + "' is not a register",
                          expr.span, "register numbers are x0 to x31",
                          "this machine has 32 registers; ABI names like a0 and sp also work");
                    return result;
                }

                std::string hint;
                std::vector<std::string_view> candidates = out_.symbols.names();
                for (const isa::CsrDesc& csr : isa::kCsrTable) candidates.push_back(csr.name);
                if (const std::string_view suggestion = closest_match(expr.name, candidates);
                    !suggestion.empty()) {
                    hint = "did you mean '" + std::string(suggestion) + "'?";
                    if (const Symbol* other = out_.symbols.find(suggestion)) {
                        hint += " (defined at line " + std::to_string(other->definition.line) + ")";
                    }
                }
                error("E0201", "undefined symbol '" + std::string(expr.name) + "'", expr.span,
                      "not defined in this file", std::move(hint));
            }
            return result;
        }

        case Expr::Kind::Unary: {
            const Value operand = eval(expr.lhs, report);
            if (!operand.ok) return result;
            if (operand.space != Space::Absolute && expr.op != '+') {
                if (report) {
                    error("E0302", "cannot apply unary '" + std::string(1, expr.op) +
                                       "' to an address",
                          expr.span, std::string(space_name(operand.space)) + " address");
                }
                return result;
            }
            switch (expr.op) {
                case '-': result.number = -operand.number; break;
                case '~': result.number = ~operand.number; break;
                case '+': result.number = operand.number; break;
                default: return result;
            }
            result.space = operand.space;
            result.ok = true;
            return result;
        }

        case Expr::Kind::HiReloc:
        case Expr::Kind::LoReloc: {
            const Value operand = eval(expr.lhs, report);
            if (!operand.ok) return result;
            const HiLo split = split_hi_lo(operand.number);
            result.number = expr.kind == Expr::Kind::HiReloc ? split.hi : split.lo;
            result.space = Space::Absolute;
            result.ok = true;
            return result;
        }

        case Expr::Kind::Binary: {
            const Value lhs = eval(expr.lhs, report);
            const Value rhs = eval(expr.rhs, report);
            if (!lhs.ok || !rhs.ok) return result;

            // Relocatable arithmetic: an address plus a constant is still an
            // address in the same space, and the difference of two addresses in
            // the same space is a plain number. Anything else mixes spaces and
            // is a mistake worth reporting rather than silently computing.
            const bool lhs_abs = lhs.space == Space::Absolute;
            const bool rhs_abs = rhs.space == Space::Absolute;

            if (expr.op == '+') {
                if (!lhs_abs && !rhs_abs) {
                    if (report) {
                        error("E0301", "cannot add two addresses", expr.span,
                              std::string(space_name(lhs.space)) + " + " + space_name(rhs.space));
                    }
                    return result;
                }
                result.number = lhs.number + rhs.number;
                result.space = lhs_abs ? rhs.space : lhs.space;
                result.ok = true;
                return result;
            }
            if (expr.op == '-') {
                if (!rhs_abs) {
                    if (lhs.space != rhs.space) {
                        if (report) {
                            error("E0301", "cannot subtract addresses from different spaces",
                                  expr.span,
                                  std::string(space_name(lhs.space)) + " - " +
                                      space_name(rhs.space),
                                  "instruction memory and data memory are separate address "
                                  "spaces; see docs/memory-map.md");
                        }
                        return result;
                    }
                    result.space = Space::Absolute;  // a distance, not a location
                } else {
                    result.space = lhs.space;
                }
                result.number = lhs.number - rhs.number;
                result.ok = true;
                return result;
            }

            if (!lhs_abs || !rhs_abs) {
                if (report) {
                    error("E0302",
                          std::string("'") + expr.op + "' needs plain numbers, not addresses",
                          expr.span);
                }
                return result;
            }
            switch (expr.op) {
                case '*': result.number = lhs.number * rhs.number; break;
                case '/':
                case '%':
                    if (rhs.number == 0) {
                        if (report) error("E0303", "division by zero in expression", expr.span);
                        return result;
                    }
                    result.number = expr.op == '/' ? lhs.number / rhs.number
                                                   : lhs.number % rhs.number;
                    break;
                case '&': result.number = lhs.number & rhs.number; break;
                case '|': result.number = lhs.number | rhs.number; break;
                case '^': result.number = lhs.number ^ rhs.number; break;
                case 'L': result.number = lhs.number << (rhs.number & 63); break;
                case 'R': result.number = lhs.number >> (rhs.number & 63); break;
                default: return result;
            }
            result.space = Space::Absolute;
            result.ok = true;
            return result;
        }
    }
    return result;
}

Value Assembler::eval_operand(const AsmLine& line, std::size_t index, bool report) {
    if (index >= line.operands.size()) return {};
    return eval(line.operands[index].expr, report);
}

// ---------------------------------------------------------------------------
// Operand accessors
// ---------------------------------------------------------------------------

bool Assembler::expect_operand_count(const AsmLine& line, std::size_t count) {
    if (line.operands.size() == count) return true;
    error("E0101",
          "'" + std::string(line.mnemonic) + "' takes " + std::to_string(count) +
              (count == 1 ? " operand" : " operands") + ", but " +
              std::to_string(line.operands.size()) + " were given",
          line.mnemonic_span, "wrong number of operands");
    return false;
}

bool Assembler::want_register(const AsmLine& line, std::size_t index, RegIdx& out) {
    if (index >= line.operands.size()) return false;
    const Operand& operand = line.operands[index];
    if (operand.kind != Operand::Kind::Register) {
        error("E0103", "operand " + std::to_string(index + 1) + " of '" +
                           std::string(line.mnemonic) + "' must be a register",
              operand.span, "not a register", "registers are x0-x31 or ABI names like a0, sp, ra");
        return false;
    }
    out = operand.reg;
    return true;
}

bool Assembler::want_value(const AsmLine& line, std::size_t index, i64& out, Space& space) {
    if (index >= line.operands.size()) return false;
    const Operand& operand = line.operands[index];
    if (operand.kind != Operand::Kind::Expression) {
        error("E0104", "operand " + std::to_string(index + 1) + " of '" +
                           std::string(line.mnemonic) + "' must be a value",
              operand.span);
        return false;
    }
    const Value value = eval(operand.expr, true);
    if (!value.ok) return false;
    out = value.number;
    space = value.space;
    return true;
}

bool Assembler::want_memory(const AsmLine& line, std::size_t index, RegIdx& base, i64& offset) {
    if (index >= line.operands.size()) return false;
    const Operand& operand = line.operands[index];

    if (operand.kind == Operand::Kind::Memory) {
        const Value value = eval(operand.expr, true);
        if (!value.ok) return false;
        base = operand.reg;
        offset = value.number;
        return true;
    }
    error("E0105", "expected an offset and base register, as in 8(sp)", operand.span,
          "not a memory operand");
    return false;
}

bool Assembler::want_compressible_register(const AsmLine& line, std::size_t index, RegIdx& out) {
    if (!want_register(line, index, out)) return false;
    if (isa::is_compressible_reg(out)) return true;
    // The single most common way to be surprised by the C extension, so it gets
    // a diagnostic under the offending operand rather than under the mnemonic,
    // and names the way out.
    error("E0107",
          "'" + std::string(line.mnemonic) + "' cannot reach " +
              std::string(isa::abi_name(out)),
          line.operands[index].span, "not one of the eight compressed registers",
          "this form encodes its registers in 3 bits, so it can only use " +
              std::string(isa::kCompressibleRegList) +
              "; use the 32-bit form for anything else");
    return false;
}

bool Assembler::want_nonzero_register(const AsmLine& line, std::size_t index, RegIdx& out) {
    if (!want_register(line, index, out)) return false;
    if (out != 0) return true;
    error("E0108", "'" + std::string(line.mnemonic) + "' cannot use zero",
          line.operands[index].span, "this operand may not be x0",
          "the encoding uses x0 in this slot to mean a different instruction");
    return false;
}

bool Assembler::want_csr(const AsmLine& line, std::size_t index, CsrAddr& out) {
    i64 value = 0;
    Space space = Space::Absolute;
    if (!want_value(line, index, value, space)) return false;
    if (value < 0 || value > 0xfff) {
        error("E0106", "csr address " + std::to_string(value) + " is out of range",
              line.operands[index].span, "must be 0..4095");
        return false;
    }
    out = static_cast<CsrAddr>(value);
    return true;
}

bool Assembler::want_code_target(const AsmLine& line, std::size_t index, Addr& target) {
    i64 value = 0;
    Space space = Space::Absolute;
    if (!want_value(line, index, value, space)) return false;

    // The Harvard check. A data symbol has a perfectly valid-looking numeric
    // value that names a completely different location, so jumping to it would
    // assemble cleanly and behave insanely. See docs/memory-map.md.
    if (space == Space::Data) {
        const Operand& operand = line.operands[index];
        Diagnostic diagnostic;
        diagnostic.code = "E0310";
        diagnostic.message = "'" + std::string(line.mnemonic) +
                             "' needs a code address, but this is a data symbol";
        diagnostic.primary = operand.span;
        diagnostic.primary_label = "lives in data memory";
        diagnostic.hint =
            "instruction memory and data memory are separate address spaces on this machine; "
            "use 'la' to load a data address into a register";
        if (program_.exprs.valid(operand.expr)) {
            const Expr& expr = program_.exprs[operand.expr];
            if (expr.kind == Expr::Kind::Symbol) {
                if (const Symbol* symbol = out_.symbols.find(expr.name)) {
                    diagnostic.labels.push_back(
                        Label{symbol->definition, "defined in .data here"});
                }
            }
        }
        out_.diagnostics.add(std::move(diagnostic));
        return false;
    }

    target = static_cast<Addr>(value);
    return true;
}

// ---------------------------------------------------------------------------
// Emission
// ---------------------------------------------------------------------------

void Assembler::emit(InstrId id, const isa::Operands& operands, SourceSpan span,
                     SourceSpan value_span) {
    if (space_ != Space::Text) {
        error("E0401", "instructions can only appear in .text", span,
              "this is inside .data");
        return;
    }
    // Instructions are a halfword stream now, so only an odd cursor is wrong.
    if ((text_.cursor & 1u) != 0) {
        error("E0402", "instruction is not halfword-aligned", span,
              "address 0x" + std::to_string(text_.cursor),
              "a preceding .byte or .space left the cursor misaligned; add '.align 1'");
        return;
    }

    const isa::EncodeResult encoded = isa::encode(id, operands);
    if (!encoded.ok()) {
        std::string label = isa::encode_error_message(encoded.error);
        std::string hint;
        if (encoded.error == isa::EncodeError::ImmediateOutOfRange) {
            label = std::string(encoded.field) + " " + std::to_string(encoded.value) +
                    " is out of range";
            hint = "must be in " + std::to_string(encoded.min) + ".." + std::to_string(encoded.max);
            const bool compressed = isa::is_compressed_format(isa::describe(id).format);
            if (compressed) {
                // The 32-bit form of the same operation is the answer, and it
                // is one word away -- much better advice than spilling to a
                // register. Asking expand() for the name rather than stripping
                // the "c." prefix keeps the suggestion honest: c.swsp becomes
                // sw and c.addi4spn becomes addi, neither of which is spelled
                // anything like its compressed form.
                isa::DecodedInstr probe;
                probe.id = id;
                const std::string_view base = isa::mnemonic_of(isa::expand(probe).id);
                hint += "; the 32-bit '" + std::string(base) + "' has room for it";
            } else if (std::string(encoded.field) == "immediate") {
                hint += "; load it first with 'li t0, " + std::to_string(encoded.value) +
                        "' and use a register";
            } else {
                hint += "; the target is too far away for this instruction";
            }
        } else if (encoded.error == isa::EncodeError::ImmediateMisaligned && encoded.min > 1) {
            // Compressed offsets are scaled, so the encoding simply has no bit
            // for the low ones. Saying which multiple is required is far more
            // use than "misaligned".
            label = std::string(encoded.field) + " " + std::to_string(encoded.value) +
                    " is not a multiple of " + std::to_string(encoded.min);
            hint = "this form scales its offset by " + std::to_string(encoded.min) +
                   ", so it cannot encode the low bits";
        } else if (encoded.error == isa::EncodeError::ImmediateMustNotBeZero) {
            label = std::string(encoded.field) + " cannot be zero in this form";
            hint = "that encoding is reserved; use the 32-bit form if you need it";
        } else if (encoded.error == isa::EncodeError::RegisterNotCompressible) {
            label = "register not reachable from this form";
            hint = "compressed forms can only use " + std::string(isa::kCompressibleRegList);
        }
        // An error about a value belongs under the value, not under the
        // mnemonic -- which matters much more for compressed instructions,
        // where a range can be as narrow as 0..124.
        const bool about_value = encoded.error != isa::EncodeError::RegisterOutOfRange &&
                                 encoded.error != isa::EncodeError::RegisterNotCompressible &&
                                 encoded.error != isa::EncodeError::RegisterMustNotBeZero;
        error("E0102",
              std::string("cannot encode '") + std::string(isa::mnemonic_of(id)) + "'",
              about_value && value_span.valid() ? value_span : span, std::move(label),
              std::move(hint));
        return;
    }

    out_.map.add(AddrEntry{text_.cursor, current_line_, current_slot_, current_count_,
                           current_pseudo_});
    ++current_slot_;
    if (encoded.size_bytes == 2) {
        text_.emit_half(static_cast<u16>(encoded.word));
    } else {
        text_.emit_word(encoded.word);
    }
}

void Assembler::begin_line(const AsmLine& line) {
    current_line_ = line.line;
    current_slot_ = 0;
    current_count_ = std::max<u8>(line.words, 1);
    current_pseudo_ = line.kind == AsmLine::Kind::Pseudo ? line.pseudo : kInvalidPseudo;
    current_line_addr_ = line.addr;
}

void Assembler::encode_instruction(const AsmLine& line) {
    const isa::InstrDesc& desc = isa::describe(line.instr);
    isa::Operands operands;
    i64 value = 0;
    Space space = Space::Absolute;
    // Where to point an encoding error that is about a value rather than the
    // instruction as a whole. Compressed immediates have tight enough ranges
    // that underlining the mnemonic is not good enough.
    SourceSpan value_span{};

    switch (desc.syntax) {
        case isa::OperandSyntax::NONE:
            if (!expect_operand_count(line, 0)) return;
            break;

        case isa::OperandSyntax::RD_RS1_RS2:
            if (!expect_operand_count(line, 3)) return;
            if (!want_register(line, 0, operands.rd)) return;
            if (!want_register(line, 1, operands.rs1)) return;
            if (!want_register(line, 2, operands.rs2)) return;
            break;

        case isa::OperandSyntax::RD_RS1_IMM:
        case isa::OperandSyntax::RD_RS1_SHAMT:
            if (!expect_operand_count(line, 3)) return;
            if (!want_register(line, 0, operands.rd)) return;
            if (!want_register(line, 1, operands.rs1)) return;
            if (!want_value(line, 2, value, space)) return;
            operands.imm = static_cast<i32>(value);
            break;

        case isa::OperandSyntax::RD_OFS_RS1: {
            // Two spellings: `lw a0, 8(sp)` and, for jalr, `jalr ra, ra, 0`.
            if (!want_register(line, 0, operands.rd)) return;
            if (line.operands.size() == 2) {
                i64 offset = 0;
                if (!want_memory(line, 1, operands.rs1, offset)) return;
                operands.imm = static_cast<i32>(offset);
            } else if (line.operands.size() == 3) {
                if (!want_register(line, 1, operands.rs1)) return;
                if (!want_value(line, 2, value, space)) return;
                operands.imm = static_cast<i32>(value);
            } else {
                error("E0101",
                      "'" + std::string(line.mnemonic) + "' takes 2 or 3 operands, but " +
                          std::to_string(line.operands.size()) + " were given",
                      line.mnemonic_span, "wrong number of operands",
                      "write it as 'rd, offset(base)' or 'rd, base, offset'");
                return;
            }
            if (options_.strict_word_mem && desc.group == isa::InstrGroup::Load &&
                (line.instr == InstrId::LB || line.instr == InstrId::LBU ||
                 line.instr == InstrId::LH || line.instr == InstrId::LHU)) {
                warn("W0501", "sub-word load needs byte enables in hardware", line.mnemonic_span,
                     "", "a word-addressed Verilog memory cannot implement this");
            }
            break;
        }

        case isa::OperandSyntax::RS2_OFS_RS1: {
            if (!expect_operand_count(line, 2)) return;
            if (!want_register(line, 0, operands.rs2)) return;
            i64 offset = 0;
            if (!want_memory(line, 1, operands.rs1, offset)) return;
            operands.imm = static_cast<i32>(offset);
            if (options_.strict_word_mem &&
                (line.instr == InstrId::SB || line.instr == InstrId::SH)) {
                warn("W0501", "sub-word store needs byte enables in hardware", line.mnemonic_span,
                     "", "a word-addressed Verilog memory cannot implement this");
            }
            break;
        }

        case isa::OperandSyntax::RS1_RS2_LBL: {
            if (!expect_operand_count(line, 3)) return;
            if (!want_register(line, 0, operands.rs1)) return;
            if (!want_register(line, 1, operands.rs2)) return;
            Addr target = 0;
            if (!want_code_target(line, 2, target)) return;
            operands.imm = static_cast<i32>(target) - static_cast<i32>(text_.cursor);
            break;
        }

        case isa::OperandSyntax::RD_UIMM:
            if (!expect_operand_count(line, 2)) return;
            if (!want_register(line, 0, operands.rd)) return;
            if (!want_value(line, 1, value, space)) return;
            if (value < -0x80000 || value > 0xfffff) {
                error("E0102", "upper immediate out of range", line.operands[1].span,
                      std::to_string(value) + " does not fit in 20 bits",
                      "lui and auipc take the upper 20 bits, so the range is 0..0xfffff");
                return;
            }
            operands.imm = static_cast<i32>(static_cast<u32>(value) << 12);
            break;

        case isa::OperandSyntax::RD_LBL: {
            if (!expect_operand_count(line, 2)) return;
            if (!want_register(line, 0, operands.rd)) return;
            Addr target = 0;
            if (!want_code_target(line, 1, target)) return;
            operands.imm = static_cast<i32>(target) - static_cast<i32>(text_.cursor);
            break;
        }

        case isa::OperandSyntax::RD_CSR_RS1:
            if (!expect_operand_count(line, 3)) return;
            if (!want_register(line, 0, operands.rd)) return;
            if (!want_csr(line, 1, operands.csr)) return;
            if (!want_register(line, 2, operands.rs1)) return;
            break;

        case isa::OperandSyntax::RD_CSR_ZIMM:
            if (!expect_operand_count(line, 3)) return;
            if (!want_register(line, 0, operands.rd)) return;
            if (!want_csr(line, 1, operands.csr)) return;
            if (!want_value(line, 2, value, space)) return;
            operands.imm = static_cast<i32>(value);
            break;

        // ---- compressed ----------------------------------------------------
        // These bind operands exactly like their 32-bit counterparts; what
        // differs is that a register can be rejected for being the wrong
        // register rather than for not being one. The range checks stay in
        // isa/encode.cpp with the bit layouts they belong to -- `value_span`
        // below is what lets those errors still point at the right operand.
        case isa::OperandSyntax::C_RD_IMM:
        case isa::OperandSyntax::C_RD_SHAMT:
            // x0 here encodes a HINT rather than an error, but writing one is
            // almost always a mistake, so the assembler refuses even though the
            // encoder would not. Disassembly of a HINT still round-trips,
            // because that path does not come through here.
            if (!expect_operand_count(line, 2)) return;
            if (!want_nonzero_register(line, 0, operands.rd)) return;
            if (!want_value(line, 1, value, space)) return;
            operands.imm = static_cast<i32>(value);
            value_span = line.operands[1].span;
            break;

        case isa::OperandSyntax::C_RD_UIMM:
            if (!expect_operand_count(line, 2)) return;
            if (!want_nonzero_register(line, 0, operands.rd)) return;
            if (!want_value(line, 1, value, space)) return;
            // Written as the upper bits, like lui.
            operands.imm = static_cast<i32>(static_cast<u32>(value) << 12);
            value_span = line.operands[1].span;
            break;

        case isa::OperandSyntax::C_RDP_IMM:
        case isa::OperandSyntax::C_RDP_SHAMT:
        case isa::OperandSyntax::C_RDP_UIMM:
            if (!expect_operand_count(line, 2)) return;
            if (!want_compressible_register(line, 0, operands.rd)) return;
            if (!want_value(line, 1, value, space)) return;
            operands.imm = static_cast<i32>(value);
            value_span = line.operands[1].span;
            break;

        case isa::OperandSyntax::C_RD_RS2:
            if (!expect_operand_count(line, 2)) return;
            if (!want_nonzero_register(line, 0, operands.rd)) return;
            if (!want_nonzero_register(line, 1, operands.rs2)) return;
            break;

        case isa::OperandSyntax::C_RDP_RS2P:
            if (!expect_operand_count(line, 2)) return;
            if (!want_compressible_register(line, 0, operands.rd)) return;
            if (!want_compressible_register(line, 1, operands.rs2)) return;
            break;

        case isa::OperandSyntax::C_RS1:
            if (!expect_operand_count(line, 1)) return;
            if (!want_nonzero_register(line, 0, operands.rs1)) return;
            break;

        case isa::OperandSyntax::C_IMM:
            if (!expect_operand_count(line, 1)) return;
            if (!want_value(line, 0, value, space)) return;
            operands.imm = static_cast<i32>(value);
            value_span = line.operands[0].span;
            break;

        case isa::OperandSyntax::C_LBL: {
            if (!expect_operand_count(line, 1)) return;
            Addr target = 0;
            if (!want_code_target(line, 0, target)) return;
            operands.imm = static_cast<i32>(target) - static_cast<i32>(text_.cursor);
            value_span = line.operands[0].span;
            break;
        }

        case isa::OperandSyntax::C_RS1P_LBL: {
            if (!expect_operand_count(line, 2)) return;
            if (!want_compressible_register(line, 0, operands.rs1)) return;
            Addr target = 0;
            if (!want_code_target(line, 1, target)) return;
            operands.imm = static_cast<i32>(target) - static_cast<i32>(text_.cursor);
            value_span = line.operands[1].span;
            break;
        }

        case isa::OperandSyntax::C_RDP_OFS_RS1P: {
            if (!expect_operand_count(line, 2)) return;
            if (!want_compressible_register(line, 0, operands.rd)) return;
            i64 offset = 0;
            if (!want_memory(line, 1, operands.rs1, offset)) return;
            if (!isa::is_compressible_reg(operands.rs1)) {
                error("E0107",
                      "'" + std::string(line.mnemonic) + "' cannot reach " +
                          std::string(isa::abi_name(operands.rs1)) + " as a base",
                      line.operands[1].span, "not one of the eight compressed registers",
                      "this form encodes its base register in 3 bits, so it can only use " +
                          std::string(isa::kCompressibleRegList) + "; use 'lw' instead");
                return;
            }
            operands.imm = static_cast<i32>(offset);
            value_span = line.operands[1].span;
            break;
        }

        case isa::OperandSyntax::C_RS2P_OFS_RS1P: {
            if (!expect_operand_count(line, 2)) return;
            if (!want_compressible_register(line, 0, operands.rs2)) return;
            i64 offset = 0;
            if (!want_memory(line, 1, operands.rs1, offset)) return;
            if (!isa::is_compressible_reg(operands.rs1)) {
                error("E0107",
                      "'" + std::string(line.mnemonic) + "' cannot reach " +
                          std::string(isa::abi_name(operands.rs1)) + " as a base",
                      line.operands[1].span, "not one of the eight compressed registers",
                      "this form encodes its base register in 3 bits, so it can only use " +
                          std::string(isa::kCompressibleRegList) + "; use 'sw' instead");
                return;
            }
            operands.imm = static_cast<i32>(offset);
            value_span = line.operands[1].span;
            break;
        }

        case isa::OperandSyntax::C_RD_OFS_SP:
        case isa::OperandSyntax::C_RS2_OFS_SP: {
            if (!expect_operand_count(line, 2)) return;
            const bool is_load = desc.syntax == isa::OperandSyntax::C_RD_OFS_SP;
            if (is_load) {
                if (!want_nonzero_register(line, 0, operands.rd)) return;
            } else {
                if (!want_register(line, 0, operands.rs2)) return;
            }
            i64 offset = 0;
            RegIdx base = 0;
            if (!want_memory(line, 1, base, offset)) return;
            if (base != 2) {
                error("E0109",
                      "'" + std::string(line.mnemonic) + "' always addresses through sp",
                      line.operands[1].span,
                      "base is " + std::string(isa::abi_name(base)) + ", not sp",
                      "the stack pointer is implied by the encoding; write it as 'sp'");
                return;
            }
            operands.rs1 = base;
            operands.imm = static_cast<i32>(offset);
            value_span = line.operands[1].span;
            break;
        }
    }

    emit(line.instr, operands, line.mnemonic_span, value_span);
}

void Assembler::encode_pseudo(const AsmLine& line) {
    const PseudoDesc& desc = describe_pseudo(line.pseudo);
    const SourceSpan span = line.mnemonic_span;
    if (!expect_operand_count(line, desc.operand_count)) return;

    const auto simple = [&](InstrId id, RegIdx rd, RegIdx rs1, RegIdx rs2, i32 imm) {
        isa::Operands operands;
        operands.rd = rd;
        operands.rs1 = rs1;
        operands.rs2 = rs2;
        operands.imm = imm;
        emit(id, operands, span);
    };

    // Registers the shapes need, read once up front.
    RegIdx rd = 0;
    RegIdx rs = 0;
    RegIdx rt = 0;
    switch (desc.shape) {
        case PseudoShape::RD_RS:
            if (!want_register(line, 0, rd) || !want_register(line, 1, rs)) return;
            break;
        case PseudoShape::RS_LBL:
        case PseudoShape::RS:
            if (!want_register(line, 0, rs)) return;
            break;
        case PseudoShape::RS_RS_LBL:
            if (!want_register(line, 0, rs) || !want_register(line, 1, rt)) return;
            break;
        case PseudoShape::RD_IMM:
        case PseudoShape::RD_SYM:
        case PseudoShape::RD_CSR:
            if (!want_register(line, 0, rd)) return;
            break;
        case PseudoShape::CSR_RS:
            if (!want_register(line, 1, rs)) return;
            break;
        case PseudoShape::NONE:
        case PseudoShape::LBL:
        case PseudoShape::CSR_IMM:
            break;
    }

    switch (line.pseudo) {
        case PseudoId::NOP: simple(InstrId::ADDI, 0, 0, 0, 0); return;
        case PseudoId::MV: simple(InstrId::ADDI, rd, rs, 0, 0); return;
        case PseudoId::NOT: simple(InstrId::XORI, rd, rs, 0, -1); return;
        case PseudoId::NEG: simple(InstrId::SUB, rd, 0, rs, 0); return;
        case PseudoId::SEQZ: simple(InstrId::SLTIU, rd, rs, 0, 1); return;
        case PseudoId::SNEZ: simple(InstrId::SLTU, rd, 0, rs, 0); return;
        case PseudoId::SLTZ: simple(InstrId::SLT, rd, rs, 0, 0); return;
        case PseudoId::SGTZ: simple(InstrId::SLT, rd, 0, rs, 0); return;

        case PseudoId::BEQZ:
        case PseudoId::BNEZ:
        case PseudoId::BLEZ:
        case PseudoId::BGEZ:
        case PseudoId::BLTZ:
        case PseudoId::BGTZ: {
            Addr target = 0;
            if (!want_code_target(line, 1, target)) return;
            const i32 offset = static_cast<i32>(target) - static_cast<i32>(text_.cursor);
            isa::Operands operands;
            operands.imm = offset;
            InstrId id = InstrId::BEQ;
            switch (line.pseudo) {
                case PseudoId::BEQZ: id = InstrId::BEQ; operands.rs1 = rs; break;
                case PseudoId::BNEZ: id = InstrId::BNE; operands.rs1 = rs; break;
                // The zero-comparison forms with the operands swapped: `blez rs`
                // is `bge x0, rs` because there is no "branch if less or equal".
                case PseudoId::BLEZ: id = InstrId::BGE; operands.rs2 = rs; break;
                case PseudoId::BGEZ: id = InstrId::BGE; operands.rs1 = rs; break;
                case PseudoId::BLTZ: id = InstrId::BLT; operands.rs1 = rs; break;
                case PseudoId::BGTZ: id = InstrId::BLT; operands.rs2 = rs; break;
                default: break;
            }
            emit(id, operands, span);
            return;
        }

        case PseudoId::BGT:
        case PseudoId::BLE:
        case PseudoId::BGTU:
        case PseudoId::BLEU: {
            Addr target = 0;
            if (!want_code_target(line, 2, target)) return;
            isa::Operands operands;
            // Swap the sources: `bgt a, b` is `blt b, a`.
            operands.rs1 = rt;
            operands.rs2 = rs;
            operands.imm = static_cast<i32>(target) - static_cast<i32>(text_.cursor);
            InstrId id = InstrId::BLT;
            switch (line.pseudo) {
                case PseudoId::BGT: id = InstrId::BLT; break;
                case PseudoId::BLE: id = InstrId::BGE; break;
                case PseudoId::BGTU: id = InstrId::BLTU; break;
                case PseudoId::BLEU: id = InstrId::BGEU; break;
                default: break;
            }
            emit(id, operands, span);
            return;
        }

        case PseudoId::J:
        case PseudoId::JAL1: {
            Addr target = 0;
            if (!want_code_target(line, 0, target)) return;
            isa::Operands operands;
            operands.rd = line.pseudo == PseudoId::J ? 0 : 1;  // x0 discards the link, ra keeps it
            operands.imm = static_cast<i32>(target) - static_cast<i32>(text_.cursor);
            emit(InstrId::JAL, operands, span);
            return;
        }

        case PseudoId::JR: simple(InstrId::JALR, 0, rs, 0, 0); return;
        case PseudoId::JALR1: simple(InstrId::JALR, 1, rs, 0, 0); return;
        case PseudoId::RET: simple(InstrId::JALR, 0, 1, 0, 0); return;

        case PseudoId::CALL:
        case PseudoId::TAIL: {
            Addr target = 0;
            if (!want_code_target(line, 0, target)) return;
            // pc-relative, and relative to the auipc rather than the jalr,
            // because auipc uses its own address.
            const i64 offset = static_cast<i64>(target) - static_cast<i64>(text_.cursor);
            const HiLo split = split_hi_lo(offset);
            const RegIdx link = line.pseudo == PseudoId::CALL ? RegIdx{1} : RegIdx{6};  // ra or t1

            isa::Operands upper;
            upper.rd = link;
            upper.imm = static_cast<i32>(static_cast<u32>(split.hi) << 12);
            emit(InstrId::AUIPC, upper, span);

            isa::Operands jump;
            jump.rd = line.pseudo == PseudoId::CALL ? RegIdx{1} : RegIdx{0};
            jump.rs1 = link;
            jump.imm = split.lo;
            emit(InstrId::JALR, jump, span);
            return;
        }

        case PseudoId::LI: {
            i64 value = 0;
            Space space = Space::Absolute;
            if (!want_value(line, 1, value, space)) return;
            if (space != Space::Absolute) {
                error("E0311", "'li' loads a constant, not an address", line.operands[1].span,
                      std::string(space_name(space)) + " symbol",
                      "use 'la' to load the address of a symbol");
                return;
            }
            if (line.words == 1) {
                simple(InstrId::ADDI, rd, 0, 0, static_cast<i32>(value));
            } else {
                const HiLo split = split_hi_lo(value);
                isa::Operands upper;
                upper.rd = rd;
                upper.imm = static_cast<i32>(static_cast<u32>(split.hi) << 12);
                emit(InstrId::LUI, upper, span);
                simple(InstrId::ADDI, rd, rd, 0, split.lo);
            }
            return;
        }

        case PseudoId::LA: {
            i64 value = 0;
            Space space = Space::Absolute;
            if (!want_value(line, 1, value, space)) return;
            // Absolute lui+addi, never auipc+addi: auipc would compute an
            // address relative to instruction memory, and a data symbol does
            // not live there. See docs/memory-map.md.
            const HiLo split = split_hi_lo(value);
            isa::Operands upper;
            upper.rd = rd;
            upper.imm = static_cast<i32>(static_cast<u32>(split.hi) << 12);
            emit(InstrId::LUI, upper, span);
            simple(InstrId::ADDI, rd, rd, 0, split.lo);

            // Explain the sizing rule once per program. It is worth saying,
            // because the disassembly will honestly show a `lui rd, 0` that
            // looks like a bug -- but saying it at every `la` in a small
            // program buries the actual diagnostics.
            if (options_.explain_pseudo_sizing && split.hi == 0 && !explained_la_sizing_) {
                explained_la_sizing_ = true;
                out_.diagnostics.note(
                    "'la' always expands to two instructions (lui + addi), even when the address "
                    "would fit in one; this is why the disassembly shows 'lui rd, 0'",
                    span, "reported once per file");
            }
            return;
        }

        case PseudoId::CSRR: {
            CsrAddr csr = 0;
            if (!want_csr(line, 1, csr)) return;
            isa::Operands operands;
            operands.rd = rd;
            operands.csr = csr;
            operands.rs1 = 0;  // x0 source: a read with no write
            emit(InstrId::CSRRS, operands, span);
            return;
        }
        case PseudoId::CSRW:
        case PseudoId::CSRS:
        case PseudoId::CSRC: {
            CsrAddr csr = 0;
            if (!want_csr(line, 0, csr)) return;
            isa::Operands operands;
            operands.rd = 0;  // x0 destination: a write with no read
            operands.csr = csr;
            operands.rs1 = rs;
            const InstrId id = line.pseudo == PseudoId::CSRW   ? InstrId::CSRRW
                               : line.pseudo == PseudoId::CSRS ? InstrId::CSRRS
                                                               : InstrId::CSRRC;
            emit(id, operands, span);
            return;
        }
        case PseudoId::CSRWI:
        case PseudoId::CSRSI:
        case PseudoId::CSRCI: {
            CsrAddr csr = 0;
            i64 value = 0;
            Space space = Space::Absolute;
            if (!want_csr(line, 0, csr)) return;
            if (!want_value(line, 1, value, space)) return;
            isa::Operands operands;
            operands.rd = 0;
            operands.csr = csr;
            operands.imm = static_cast<i32>(value);
            const InstrId id = line.pseudo == PseudoId::CSRWI   ? InstrId::CSRRWI
                               : line.pseudo == PseudoId::CSRSI ? InstrId::CSRRSI
                                                                : InstrId::CSRRCI;
            emit(id, operands, span);
            return;
        }

        case PseudoId::Count:
            return;
    }
}

// ---------------------------------------------------------------------------
// Directives
// ---------------------------------------------------------------------------

void Assembler::run_directive_layout(const AsmLine& line, AsmLine& mutable_line) {
    const auto constant = [&](std::size_t index, i64& out) {
        const Value value = eval_operand(line, index, true);
        if (!value.ok) return false;
        if (value.space != Space::Absolute) {
            error("E0302", "this directive needs a constant, not an address",
                  line.operands[index].span);
            return false;
        }
        out = value.number;
        return true;
    };

    switch (line.directive) {
        case DirectiveId::TEXT: space_ = Space::Text; return;
        case DirectiveId::DATA: space_ = Space::Data; return;

        case DirectiveId::WORD:
        case DirectiveId::HALF:
        case DirectiveId::BYTE: {
            const u32 width = line.directive == DirectiveId::WORD   ? 4
                              : line.directive == DirectiveId::HALF ? 2
                                                                    : 1;
            if (line.operands.empty()) {
                error("E0110", "'" + std::string(line.mnemonic) + "' needs at least one value",
                      line.mnemonic_span);
                return;
            }
            const u32 size = width * static_cast<u32>(line.operands.size());
            mutable_line.words = static_cast<u8>(std::min<u32>(size, 255));
            section().skip(size);
            if (space_ == Space::Text && options_.explain_pseudo_sizing) {
                warn("W0502",
                     "'" + std::string(line.mnemonic) + "' inside .text emits into instruction "
                     "memory",
                     line.mnemonic_span, "cannot be read with lw",
                     "instruction and data memory are separate here; put data in .data");
            }
            return;
        }

        case DirectiveId::ASCII:
        case DirectiveId::ASCIZ:
        case DirectiveId::STRING: {
            const bool terminated = line.directive != DirectiveId::ASCII;
            u32 size = 0;
            for (const Operand& operand : line.operands) {
                if (operand.kind != Operand::Kind::Text) {
                    error("E0111", "expected a quoted string", operand.span);
                    return;
                }
                size += static_cast<u32>(operand.text.size()) + (terminated ? 1u : 0u);
            }
            section().skip(size);
            return;
        }

        case DirectiveId::SPACE:
        case DirectiveId::ZERO: {
            i64 count = 0;
            if (!constant(0, count)) return;
            if (count < 0) {
                error("E0112", "size cannot be negative", line.operands[0].span);
                return;
            }
            section().skip(static_cast<u32>(count));
            return;
        }

        case DirectiveId::ALIGN: {
            i64 power = 0;
            if (!constant(0, power)) return;
            if (power < 0 || power > 12) {
                error("E0113", "alignment must be between 0 and 12", line.operands[0].span,
                      "this is a power of two: '.align 2' means 4-byte alignment");
                return;
            }
            const u32 boundary = 1u << power;
            Section& current = section();
            while ((current.cursor % boundary) != 0) current.emit_byte(0);
            return;
        }

        case DirectiveId::ORG: {
            i64 target = 0;
            if (!constant(0, target)) return;
            if (target < 0) {
                error("E0112", "address cannot be negative", line.operands[0].span);
                return;
            }
            Section& current = section();
            const Addr abs = static_cast<Addr>(target);
            if (abs < current.origin) {
                error("E0114", "'.org' address is before the start of this space",
                      line.operands[0].span, "",
                      "this machine's " +
                          std::string(space_ == Space::Text ? ".text" : ".data") + " starts at 0x" +
                          [&] {
                              char buf[16];
                              std::snprintf(buf, sizeof buf, "%08x", current.origin);
                              return std::string(buf);
                          }());
                return;
            }
            current.reserve_to(current.index_of(abs));
            current.cursor = abs;
            return;
        }

        case DirectiveId::EQU:
        case DirectiveId::SET: {
            if (line.operands.size() != 2) {
                error("E0114", "'" + std::string(line.mnemonic) + "' takes a name and a value",
                      line.mnemonic_span);
                return;
            }
            const Operand& name_operand = line.operands[0];
            if (!program_.exprs.valid(name_operand.expr) ||
                program_.exprs[name_operand.expr].kind != Expr::Kind::Symbol) {
                error("E0115", "expected a symbol name", name_operand.span);
                return;
            }
            const Value value = eval_operand(line, 1, true);
            if (!value.ok) return;
            const std::string_view name = program_.exprs[name_operand.expr].name;
            if (out_.symbols.define(std::string(name), value.space,
                                    static_cast<Addr>(value.number), name_operand.span) == nullptr) {
                const Symbol* existing = out_.symbols.find(name);
                Diagnostic diagnostic;
                diagnostic.code = "E0202";
                diagnostic.message = "symbol '" + std::string(name) + "' is already defined";
                diagnostic.primary = name_operand.span;
                diagnostic.primary_label = "redefined here";
                if (existing != nullptr) {
                    diagnostic.labels.push_back(Label{existing->definition, "first defined here"});
                }
                out_.diagnostics.add(std::move(diagnostic));
            }
            return;
        }

        case DirectiveId::GLOBL:
        case DirectiveId::GLOBAL:
            // Recorded in pass 2, once the symbol it names exists.
            return;

        case DirectiveId::Count:
            return;
    }
}

void Assembler::run_directive_encode(const AsmLine& line) {
    switch (line.directive) {
        case DirectiveId::TEXT: space_ = Space::Text; return;
        case DirectiveId::DATA: space_ = Space::Data; return;

        case DirectiveId::WORD:
        case DirectiveId::HALF:
        case DirectiveId::BYTE: {
            const int width = line.directive == DirectiveId::WORD   ? 4
                              : line.directive == DirectiveId::HALF ? 2
                                                                    : 1;
            for (std::size_t i = 0; i < line.operands.size(); ++i) {
                const Value value = eval_operand(line, i, true);
                const u32 bits = value.ok ? static_cast<u32>(value.number) : 0u;
                if (value.ok && width < 4 && !fits_signed(value.number, 8 * width) &&
                    !fits_unsigned(static_cast<u64>(value.number), 8 * width)) {
                    warn("W0503", "value does not fit and will be truncated",
                         line.operands[i].span,
                         std::to_string(value.number) + " needs more than " +
                             std::to_string(8 * width) + " bits");
                }
                Section& current = section();
                if (width == 4) {
                    current.emit_word(bits);
                } else if (width == 2) {
                    current.emit_half(static_cast<u16>(bits));
                } else {
                    current.emit_byte(static_cast<u8>(bits));
                }
            }
            return;
        }

        case DirectiveId::ASCII:
        case DirectiveId::ASCIZ:
        case DirectiveId::STRING: {
            const bool terminated = line.directive != DirectiveId::ASCII;
            for (const Operand& operand : line.operands) {
                if (operand.kind != Operand::Kind::Text) return;
                for (const char character : operand.text) {
                    section().emit_byte(static_cast<u8>(character));
                }
                if (terminated) section().emit_byte(0);
            }
            return;
        }

        case DirectiveId::SPACE:
        case DirectiveId::ZERO: {
            const Value value = eval_operand(line, 0, false);
            if (value.ok && value.number >= 0) section().skip(static_cast<u32>(value.number));
            return;
        }

        case DirectiveId::ALIGN: {
            const Value value = eval_operand(line, 0, false);
            if (!value.ok || value.number < 0 || value.number > 12) return;
            const u32 boundary = 1u << value.number;
            Section& current = section();
            while ((current.cursor % boundary) != 0) current.emit_byte(0);
            return;
        }

        case DirectiveId::ORG: {
            const Value value = eval_operand(line, 0, false);
            if (!value.ok || value.number < 0) return;
            Section& current = section();
            const Addr abs = static_cast<Addr>(value.number);
            if (abs < current.origin) return;
            current.reserve_to(current.index_of(abs));
            current.cursor = abs;
            return;
        }

        case DirectiveId::GLOBL:
        case DirectiveId::GLOBAL: {
            for (const Operand& operand : line.operands) {
                if (!program_.exprs.valid(operand.expr)) continue;
                const Expr& expr = program_.exprs[operand.expr];
                if (expr.kind != Expr::Kind::Symbol) continue;
                if (out_.symbols.find(expr.name) == nullptr) {
                    error("E0201", "undefined symbol '" + std::string(expr.name) + "'",
                          operand.span, "declared global but never defined");
                    continue;
                }
                out_.symbols.mark_global(expr.name);
            }
            return;
        }

        case DirectiveId::EQU:
        case DirectiveId::SET:
        case DirectiveId::Count:
            return;  // handled during layout
    }
}

// ---------------------------------------------------------------------------
// Passes
// ---------------------------------------------------------------------------

void Assembler::pass_layout() {
    space_ = Space::Text;
    text_.origin = options_.imem_base;
    text_.cursor = options_.imem_base;
    text_.bytes.clear();
    data_.origin = options_.dmem_base;
    data_.cursor = options_.dmem_base;
    data_.bytes.clear();
    for (AsmLine& line : program_.lines) {
        current_line_ = line.line;
        current_line_addr_ = cursor();
        line.space = space_;
        line.addr = cursor();

        for (std::size_t i = 0; i < line.labels.size(); ++i) {
            const std::string_view name = line.labels[i];
            const SourceSpan where = line.label_spans[i];
            if (out_.symbols.define(std::string(name), space_, cursor(), where) != nullptr) {
                continue;
            }
            const Symbol* existing = out_.symbols.find(name);
            Diagnostic diagnostic;
            diagnostic.code = "E0202";
            diagnostic.message = "symbol '" + std::string(name) + "' is already defined";
            diagnostic.primary = where;
            diagnostic.primary_label = "redefined here";
            if (existing != nullptr) {
                diagnostic.labels.push_back(Label{existing->definition, "first defined here"});
            }
            out_.diagnostics.add(std::move(diagnostic));
        }

        switch (line.kind) {
            case AsmLine::Kind::Empty:
                break;
            case AsmLine::Kind::Error:
                // Reserve a word so that a single typo does not shift every
                // address after it and turn one error into twenty.
                if (space_ == Space::Text) section().skip(4);
                break;
            case AsmLine::Kind::Instruction:
                // `words` stays an instruction count -- it feeds the debugger's
                // "[1/2]" slot display -- while the cursor advances by the real
                // encoded width, which is 2 for a compressed row.
                //
                // This is only safe because the assembler never compresses on
                // its own: the width is a property of the mnemonic, known here
                // in pass 1, so pass 2 cannot disagree. An automatic-compression
                // mode would have to make that choice here too, never in
                // pass_encode(), or every address after it would silently shift.
                line.words = 1;
                section().skip(isa::describe(line.instr).length());
                break;
            case AsmLine::Kind::Pseudo: {
                u8 words = describe_pseudo(line.pseudo).words;
                if (line.pseudo == PseudoId::LI && line.operands.size() == 2) {
                    // The one pseudo whose size can be settled exactly: its
                    // operand is a literal the parser has already read, so no
                    // forward reference is involved. A symbolic operand cannot
                    // be folded yet and conservatively takes two words.
                    const Value value = eval_operand(line, 1, false);
                    words = value.ok && value.space == Space::Absolute
                                ? li_word_count(value.number)
                                : 2;
                }
                line.words = words;
                section().skip(4u * words);
                break;
            }
            case AsmLine::Kind::Directive:
                run_directive_layout(line, line);
                break;
        }
    }
}

void Assembler::pass_encode() {
    space_ = Space::Text;
    text_.rewind_to_origin();
    data_.rewind_to_origin();

    for (const AsmLine& line : program_.lines) {
        begin_line(line);
        switch (line.kind) {
            case AsmLine::Kind::Empty: break;
            case AsmLine::Kind::Error:
                // Already diagnosed by the parser. Skip the reserved word so
                // that later addresses match what layout computed.
                if (space_ == Space::Text) text_.skip(4);
                break;
            case AsmLine::Kind::Instruction: encode_instruction(line); break;
            case AsmLine::Kind::Pseudo: encode_pseudo(line); break;
            case AsmLine::Kind::Directive: run_directive_encode(line); break;
        }
    }
}

void Assembler::run() {
    program_ = parse(source_, out_.diagnostics);
    pass_layout();
    // Encoding runs even when parsing found problems. A student with five
    // mistakes should see all five, and because a failed line still reserves
    // its word, the addresses pass 2 works with stay correct.
    pass_encode();
    out_.map.finalize();

    if (const Symbol* start = out_.symbols.find("_start")) {
        if (start->space == Space::Text) out_.entry = start->value;
    } else if (options_.reset_entry != 0) {
        out_.entry = options_.reset_entry;
    } else {
        out_.entry = options_.imem_base;
    }

    if (text_.bytes.size() > options_.imem_size) {
        error("E0601",
              "program does not fit in instruction memory (" +
                  std::to_string(text_.bytes.size()) + " bytes, capacity " +
                  std::to_string(options_.imem_size) + ")",
              SourceSpan{});
    }
    if (data_.bytes.size() > options_.dmem_size) {
        error("E0602",
              "data does not fit in data memory (" + std::to_string(data_.bytes.size()) +
                  " bytes, capacity " + std::to_string(options_.dmem_size) + ")",
              SourceSpan{});
    }

    out_.imem_bytes = text_.bytes.size();
    out_.imem_words = core::pack_bytes_to_words(text_.bytes);
    out_.dmem_bytes = data_.bytes;
}

}  // namespace

AssembledProgram assemble(const SourceFile& source, const AssembleOptions& options) {
    AssembledProgram out;
    Assembler assembler(source, options, out);
    assembler.run();
    return out;
}

AssembledProgram assemble_text(std::string text, std::string name,
                               const AssembleOptions& options) {
    const SourceFile source(std::move(name), std::move(text));
    return assemble(source, options);
}

}  // namespace rv::as
