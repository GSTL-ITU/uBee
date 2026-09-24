#include "asm/complete.hpp"

#include <algorithm>
#include <cctype>

#include "asm/directive.hpp"
#include "asm/lexer.hpp"
#include "asm/pseudo.hpp"
#include "isa/csr.hpp"
#include "isa/isa.hpp"
#include "isa/regnames.hpp"

namespace rv::as {
namespace {

constexpr const char* kRegForm = "reg(x0-x31, a0, sp, …)";
constexpr const char* kImm12Form = "imm(-2048..2047)";
constexpr const char* kShamtForm = "imm(0..31)";
constexpr const char* kMemForm = "mem(offset(base))";
constexpr const char* kLabelForm = "label";
constexpr const char* kUpperForm = "imm(0..0xfffff)";
constexpr const char* kCsrForm = "csr(mstatus, mtvec, …)";
constexpr const char* kZimmForm = "imm(0..31)";

// Compressed forms. Every one of these ranges is narrower than its 32-bit
// equivalent, which is the whole reason they are spelled out separately: the
// point of the hint is to say what will actually assemble.
constexpr const char* kCRegForm = "reg(s0, s1, a0-a5)";
constexpr const char* kCImm6Form = "imm(-32..31)";
constexpr const char* kCShamtForm = "imm(0..31)";
constexpr const char* kCUpperForm = "imm(-32..31, not 0)";
constexpr const char* kCSpImmForm = "imm(-512..496, x16)";
constexpr const char* kC4SpnForm = "imm(4..1020, x4)";
constexpr const char* kCMemForm = "mem(0..124(base), x4)";
constexpr const char* kCSpMemForm = "mem(0..252(sp), x4)";

/// The operand shapes of a real instruction, read straight off its row. One
/// case per operand syntax rather than one per instruction, which is what keeps
/// this from having to be maintained alongside the table.
std::vector<OperandHint> hints_for(isa::OperandSyntax syntax) {
    switch (syntax) {
        case isa::OperandSyntax::NONE:
            return {};
        case isa::OperandSyntax::RD_RS1_RS2:
            return {{"rd", kRegForm}, {"rs1", kRegForm}, {"rs2", kRegForm}};
        case isa::OperandSyntax::RD_RS1_IMM:
            return {{"rd", kRegForm}, {"rs1", kRegForm}, {"imm", kImm12Form}};
        case isa::OperandSyntax::RD_RS1_SHAMT:
            return {{"rd", kRegForm}, {"rs1", kRegForm}, {"shamt", kShamtForm}};
        case isa::OperandSyntax::RD_OFS_RS1:
            return {{"rd", kRegForm}, {"offset(base)", kMemForm}};
        case isa::OperandSyntax::RS2_OFS_RS1:
            return {{"rs2", kRegForm}, {"offset(base)", kMemForm}};
        case isa::OperandSyntax::RS1_RS2_LBL:
            return {{"rs1", kRegForm}, {"rs2", kRegForm}, {"label", kLabelForm}};
        case isa::OperandSyntax::RD_UIMM:
            return {{"rd", kRegForm}, {"imm20", kUpperForm}};
        case isa::OperandSyntax::RD_LBL:
            return {{"rd", kRegForm}, {"label", kLabelForm}};
        case isa::OperandSyntax::RD_CSR_RS1:
            return {{"rd", kRegForm}, {"csr", kCsrForm}, {"rs1", kRegForm}};
        case isa::OperandSyntax::RD_CSR_ZIMM:
            return {{"rd", kRegForm}, {"csr", kCsrForm}, {"zimm", kZimmForm}};

        // ---- compressed ----------------------------------------------------
        case isa::OperandSyntax::C_RD_IMM:
            return {{"rd", kRegForm}, {"imm", kCImm6Form}};
        case isa::OperandSyntax::C_RD_UIMM:
            return {{"rd", kRegForm}, {"imm", kCUpperForm}};
        case isa::OperandSyntax::C_RD_SHAMT:
            return {{"rd", kRegForm}, {"shamt", kCShamtForm}};
        case isa::OperandSyntax::C_RD_RS2:
            return {{"rd", kRegForm}, {"rs2", kRegForm}};
        case isa::OperandSyntax::C_RDP_IMM:
            return {{"rd'", kCRegForm}, {"imm", kCImm6Form}};
        case isa::OperandSyntax::C_RDP_UIMM:
            return {{"rd'", kCRegForm}, {"imm", kC4SpnForm}};
        case isa::OperandSyntax::C_RDP_SHAMT:
            return {{"rd'", kCRegForm}, {"shamt", kCShamtForm}};
        case isa::OperandSyntax::C_RDP_RS2P:
            return {{"rd'", kCRegForm}, {"rs2'", kCRegForm}};
        case isa::OperandSyntax::C_RS1:
            return {{"rs1", kRegForm}};
        case isa::OperandSyntax::C_IMM:
            return {{"imm", kCSpImmForm}};
        case isa::OperandSyntax::C_LBL:
            return {{"label", kLabelForm}};
        case isa::OperandSyntax::C_RS1P_LBL:
            return {{"rs1'", kCRegForm}, {"label", kLabelForm}};
        case isa::OperandSyntax::C_RDP_OFS_RS1P:
            return {{"rd'", kCRegForm}, {"offset(base')", kCMemForm}};
        case isa::OperandSyntax::C_RS2P_OFS_RS1P:
            return {{"rs2'", kCRegForm}, {"offset(base')", kCMemForm}};
        case isa::OperandSyntax::C_RD_OFS_SP:
            return {{"rd", kRegForm}, {"offset(sp)", kCSpMemForm}};
        case isa::OperandSyntax::C_RS2_OFS_SP:
            return {{"rs2", kRegForm}, {"offset(sp)", kCSpMemForm}};
    }
    return {};
}

std::vector<OperandHint> hints_for(PseudoShape shape) {
    switch (shape) {
        case PseudoShape::NONE: return {};
        case PseudoShape::RD_RS: return {{"rd", kRegForm}, {"rs", kRegForm}};
        case PseudoShape::RS_LBL: return {{"rs", kRegForm}, {"label", kLabelForm}};
        case PseudoShape::RS_RS_LBL:
            return {{"rs", kRegForm}, {"rt", kRegForm}, {"label", kLabelForm}};
        case PseudoShape::LBL: return {{"label", kLabelForm}};
        case PseudoShape::RS: return {{"rs", kRegForm}};
        case PseudoShape::RD_IMM: return {{"rd", kRegForm}, {"imm", "imm(any 32-bit value)"}};
        case PseudoShape::RD_SYM: return {{"rd", kRegForm}, {"symbol", "label(a data address)"}};
        case PseudoShape::RD_CSR: return {{"rd", kRegForm}, {"csr", kCsrForm}};
        case PseudoShape::CSR_RS: return {{"csr", kCsrForm}, {"rs", kRegForm}};
        case PseudoShape::CSR_IMM: return {{"csr", kCsrForm}, {"imm", kZimmForm}};
    }
    return {};
}

/// A one-line description of what an instruction group does, so the list says
/// something beyond the mnemonic.
std::string_view detail_for(isa::InstrGroup group) { return isa::group_name(group); }

bool starts_with(std::string_view text, std::string_view prefix) {
    return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

/// Case-insensitive prefix test, so typing in either case finds things.
bool matches(std::string_view candidate, std::string_view prefix) {
    if (prefix.empty()) return true;
    if (candidate.size() < prefix.size()) return false;
    for (std::size_t index = 0; index < prefix.size(); ++index) {
        const char a = static_cast<char>(std::tolower(static_cast<unsigned char>(candidate[index])));
        const char b = static_cast<char>(std::tolower(static_cast<unsigned char>(prefix[index])));
        if (a != b) return false;
    }
    return true;
}

/// What an empty field shows. The name is enough when it already says what
/// goes there (`rd`, `label`, `offset(base)`); an immediate is not -- "imm"
/// does not tell you that 5000 will be rejected, and its range does.
std::string_view placeholder_of(const OperandHint& hint) {
    return starts_with(hint.form, "imm(") ? hint.form : hint.name;
}

bool is_word_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_' || c == '.' || c == '$';
}

}  // namespace

// ---------------------------------------------------------------------------
// Signatures
// ---------------------------------------------------------------------------

std::vector<OperandHint> operand_hints(std::string_view mnemonic) {
    if (const isa::InstrId id = isa::lookup_mnemonic(mnemonic); id != isa::kInvalidInstr) {
        return hints_for(isa::describe(id).syntax);
    }
    // A pseudo may exist at more than one arity -- `jal label` and
    // `jal rd, label`. The longest form is the more informative hint.
    std::vector<OperandHint> best;
    for (const PseudoDesc& desc : kPseudoTable) {
        if (desc.spelling != mnemonic) continue;
        std::vector<OperandHint> hints = hints_for(desc.shape);
        if (hints.size() > best.size()) best = std::move(hints);
    }
    return best;
}

std::string signature_of(std::string_view mnemonic) {
    const std::vector<OperandHint> hints = operand_hints(mnemonic);
    std::string out(mnemonic);
    for (std::size_t index = 0; index < hints.size(); ++index) {
        out += index == 0 ? " " : ", ";
        out += hints[index].name;
    }
    return out;
}

// ---------------------------------------------------------------------------
// Context
// ---------------------------------------------------------------------------

Context context_at(std::string_view line, u16 column) {
    Context context;
    column = static_cast<u16>(std::min<std::size_t>(column, line.size()));

    // The word under the caret is whatever runs back from it.
    std::size_t begin = column;
    while (begin > 0 && is_word_char(line[begin - 1])) --begin;
    context.prefix = std::string(line.substr(begin, column - begin));
    context.prefix_begin = static_cast<u16>(begin);

    // Everything before the caret decides where we are. Re-lexing the prefix is
    // cheap and means the answer agrees with what the assembler would see.
    const std::string_view before = line.substr(0, column);
    const std::vector<Token> tokens = tokenize_for_highlight(before, 1);

    std::size_t commas = 0;
    std::string mnemonic;
    u16 mnemonic_end = 0;
    u16 field_begin = 0;
    bool seen_statement = false;

    for (const Token& token : tokens) {
        if (token.kind == TokenKind::End) break;

        // Nothing to suggest inside prose. An unterminated string lexes as an
        // error token in tolerant mode, which is the normal state of a line
        // being typed, so it counts too.
        const bool is_prose =
            token.kind == TokenKind::Comment || token.kind == TokenKind::StringLit ||
            (token.kind == TokenKind::Error && !token.text.empty() && token.text.front() == '"');
        if (is_prose) {
            if (column > token.span.col) {
                context.where = Context::Where::Nowhere;
                return context;
            }
            continue;
        }

        if (token.kind == TokenKind::Label) continue;  // labels precede a statement
        if (!seen_statement) {
            if (token.kind == TokenKind::Ident || token.kind == TokenKind::Directive) {
                mnemonic = std::string(token.text);
                mnemonic_end = token.span.end_col();
                field_begin = mnemonic_end;
                seen_statement = true;
            }
            continue;
        }
        if (token.kind == TokenKind::Comma) {
            ++commas;
            field_begin = token.span.end_col();
        }
    }

    // Still inside the leading word. Compared by position rather than by
    // length: the caret is in the mnemonic exactly while it has not passed the
    // end of that token.
    if (!seen_statement || column <= mnemonic_end) {
        context.where = Context::Where::Mnemonic;
        return context;
    }

    context.where = Context::Where::Operand;
    context.mnemonic = mnemonic;
    context.operand_index = commas;
    context.field_begin = field_begin;
    return context;
}

std::string ghost_text(std::string_view line, u16 column) {
    column = static_cast<u16>(std::min<std::size_t>(column, line.size()));

    // The ghost is painted in the space after the caret, so it may only appear
    // when that space is empty. Anything real out there wins.
    for (const char c : line.substr(column)) {
        if (std::isspace(static_cast<unsigned char>(c)) == 0) return {};
    }

    const Context context = context_at(line, column);
    if (context.where != Context::Where::Operand) return {};
    const std::vector<OperandHint> hints = operand_hints(context.mnemonic);
    if (hints.empty()) return {};

    // Is this field already being written? Not the same question as "is there
    // a word under the caret": after `lw a0, 4(` the field is well under way
    // with no word in progress.
    bool started = false;
    for (std::size_t index = context.field_begin; index < column; ++index) {
        if (std::isspace(static_cast<unsigned char>(line[index])) == 0) {
            started = true;
            break;
        }
    }

    // A started field is finished by the writer, not by us; the ghost picks up
    // at the next one and carries the comma that separates them.
    const std::size_t first = context.operand_index + (started ? 1 : 0);
    if (first >= hints.size()) return {};

    std::string out;
    if (started) {
        out += ", ";
    } else if (column > 0 && std::isspace(static_cast<unsigned char>(line[column - 1])) == 0) {
        out += ' ';  // `addi a0,` -- keep the ghost from touching the comma
    }
    for (std::size_t index = first; index < hints.size(); ++index) {
        if (index != first) out += ", ";
        out += index == first ? placeholder_of(hints[index]) : std::string_view(hints[index].name);
    }
    return out;
}

// ---------------------------------------------------------------------------
// Completion
// ---------------------------------------------------------------------------

std::vector<Completion> complete(std::string_view line, u16 column,
                                 const SymbolTable* symbols) {
    const Context context = context_at(line, column);
    std::vector<Completion> out;
    if (context.where == Context::Where::Nowhere) return out;

    const std::string& prefix = context.prefix;

    if (context.where == Context::Where::Mnemonic) {
        for (const isa::InstrDesc& desc : isa::kInstrTable) {
            if (!matches(desc.mnemonic, prefix)) continue;
            out.push_back(Completion{std::string(desc.mnemonic), signature_of(desc.mnemonic),
                                     std::string(detail_for(desc.group)),
                                     CompletionKind::Instruction});
        }
        for (const PseudoDesc& desc : kPseudoTable) {
            if (!matches(desc.spelling, prefix)) continue;
            // Skip a second entry for a spelling that also exists as a real
            // instruction, or `jal` would appear twice.
            const bool shadowed =
                isa::lookup_mnemonic(desc.spelling) != isa::kInvalidInstr ||
                std::any_of(out.begin(), out.end(),
                            [&](const Completion& c) { return c.text == desc.spelling; });
            if (shadowed) continue;
            out.push_back(Completion{std::string(desc.spelling), signature_of(desc.spelling),
                                     "pseudo-instruction", CompletionKind::Pseudo});
        }
        if (starts_with(prefix, ".") || prefix.empty()) {
            for (const std::string_view name : kDirectiveNames) {
                if (!matches(name, prefix)) continue;
                out.push_back(
                    Completion{std::string(name), std::string(name), "directive",
                               CompletionKind::Directive});
            }
        }
    } else {
        const std::vector<OperandHint> hints = operand_hints(context.mnemonic);
        const OperandHint* hint =
            context.operand_index < hints.size() ? &hints[context.operand_index] : nullptr;
        const std::string form = hint != nullptr ? hint->form : std::string();

        // Offer what actually belongs in this position rather than everything.
        // An exact compare, so every register-shaped form has to be listed --
        // miss one and register completion silently switches off for it.
        const bool wants_register = form == kRegForm || form == kCRegForm;
        const bool wants_csr = form == kCsrForm;
        const bool wants_label = starts_with(form, "label");

        if (wants_register || form.empty()) {
            for (RegIdx reg = 0; reg < kNumRegs; ++reg) {
                for (const std::string_view name :
                     {isa::abi_name(reg), isa::numeric_name(reg)}) {
                    if (!matches(name, prefix)) continue;
                    out.push_back(Completion{
                        std::string(name),
                        std::string(isa::numeric_name(reg)) + " / " +
                            std::string(isa::abi_name(reg)),
                        "register", CompletionKind::Register});
                }
            }
        }
        if (wants_csr || form.empty()) {
            for (const isa::CsrDesc& csr : isa::kCsrTable) {
                if (!matches(csr.name, prefix)) continue;
                char address[16];
                std::snprintf(address, sizeof address, "0x%03x", csr.addr);
                out.push_back(Completion{std::string(csr.name), std::string(csr.name), address,
                                         CompletionKind::Csr});
            }
        }
        if ((wants_label || form.empty()) && symbols != nullptr) {
            for (const Symbol& symbol : symbols->all()) {
                if (!matches(symbol.name, prefix)) continue;
                out.push_back(Completion{symbol.name, symbol.name,
                                         std::string(space_name(symbol.space)) + " symbol, line " +
                                             std::to_string(symbol.definition.line),
                                         CompletionKind::Symbol});
            }
        }
    }

    // An exact prefix match is what the person is most likely reaching for, and
    // shorter names come first so `add` sits above `addi`.
    std::sort(out.begin(), out.end(), [](const Completion& a, const Completion& b) {
        if (a.text.size() != b.text.size()) return a.text.size() < b.text.size();
        return a.text < b.text;
    });
    return out;
}

}  // namespace rv::as
