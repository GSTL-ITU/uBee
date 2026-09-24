#include "gui/encoding_widget.hpp"

#include <QFontDatabase>
#include <QPainter>
#include <QStringList>

#include "isa/decode.hpp"
#include "isa/disasm.hpp"
#include "isa/encode.hpp"
#include "isa/expand.hpp"
#include "isa/regnames.hpp"

namespace rv::gui {
namespace {

struct BitField {
    QString label;   // what the field is called in the spec
    QString detail;  // what it means for this particular instruction
    int hi = 0;
    int lo = 0;
    QColor colour;

    int bits() const { return hi - lo + 1; }
};

const QColor kMnemonicColour(0x6c, 0xb6, 0xff);
const QColor kRegisterColour(0x5b, 0xd6, 0xc8);
const QColor kNumberColour(0xe8, 0xa0, 0x4e);
const QColor kDimColour(0x7d, 0x85, 0x90);
const QColor kTextColour(0xd6, 0xd6, 0xd6);
const QColor kFrameColour(0x45, 0x4b, 0x56);

QString hex(u32 value) { return "0x" + QString::number(value, 16); }

QString register_detail(RegIdx reg) {
    return QString::fromLatin1(isa::numeric_name(reg).data(),
                               static_cast<int>(isa::numeric_name(reg).size())) +
           " (" +
           QString::fromLatin1(isa::abi_name(reg).data(),
                               static_cast<int>(isa::abi_name(reg).size())) +
           ")";
}

/// The fields of an instruction, left to right, in the layout its format
/// actually uses. This is the whole of the format knowledge here; everything
/// else is generic drawing.
QList<BitField> fields_of(const isa::DecodedInstr& instr) {
    const isa::InstrDesc& desc = instr.desc();
    const u32 word = instr.raw;

    const BitField opcode{"opcode",
                          hex(bits(word, 6, 0)) + " " +
                              QString::fromLatin1(desc.mnemonic.data(),
                                                  static_cast<int>(desc.mnemonic.size())),
                          6, 0, kMnemonicColour};
    const BitField funct3{"funct3", hex(bits(word, 14, 12)), 14, 12, kDimColour};
    const BitField funct7{"funct7", hex(bits(word, 31, 25)), 31, 25, kDimColour};
    const BitField rd{"rd", register_detail(instr.rd), 11, 7, kRegisterColour};
    const BitField rs1{"rs1", register_detail(instr.rs1), 19, 15, kRegisterColour};
    const BitField rs2{"rs2", register_detail(instr.rs2), 24, 20, kRegisterColour};

    // Compressed layouts. The op field is two bits, not seven, and the register
    // fields move and shrink -- naming which three bits hold a register is most
    // of what makes a 16-bit encoding readable at all.
    const BitField cop{"op",
                       hex(bits(word, 1, 0)) + " " +
                           QString::fromLatin1(desc.mnemonic.data(),
                                               static_cast<int>(desc.mnemonic.size())),
                       1, 0, kMnemonicColour};
    const BitField cfunct3{"funct3", hex(bits(word, 15, 13)), 15, 13, kDimColour};
    const BitField crd{"rd/rs1", register_detail(instr.rd), 11, 7, kRegisterColour};
    const BitField crs1{"rs1", register_detail(instr.rs1), 11, 7, kRegisterColour};
    const BitField crs2{"rs2", register_detail(instr.rs2), 6, 2, kRegisterColour};
    const BitField crs1p{"rs1'", register_detail(instr.rs1), 9, 7, kRegisterColour};
    const BitField crdp{"rd'", register_detail(instr.rd), 9, 7, kRegisterColour};
    const BitField crs2p{"rs2'", register_detail(instr.rs2), 4, 2, kRegisterColour};
    const BitField crdp_low{"rd'", register_detail(instr.rd), 4, 2, kRegisterColour};

    switch (desc.format) {
        case isa::InstrFormat::R:
            return {funct7, rs2, rs1, funct3, rd, opcode};

        case isa::InstrFormat::I:
            if (desc.syntax == isa::OperandSyntax::RD_RS1_SHAMT) {
                return {funct7,
                        {"shamt", QString::number(instr.imm), 24, 20, kNumberColour},
                        rs1, funct3, rd, opcode};
            }
            if (desc.syntax == isa::OperandSyntax::RD_CSR_RS1 ||
                desc.syntax == isa::OperandSyntax::RD_CSR_ZIMM) {
                const bool immediate = desc.syntax == isa::OperandSyntax::RD_CSR_ZIMM;
                return {{"csr", hex(instr.csr), 31, 20, kNumberColour},
                        immediate ? BitField{"zimm", QString::number(instr.imm), 19, 15,
                                             kNumberColour}
                                  : rs1,
                        funct3, rd, opcode};
            }
            return {{"imm[11:0]", QString::number(instr.imm), 31, 20, kNumberColour},
                    rs1, funct3, rd, opcode};

        case isa::InstrFormat::S:
            return {{"imm[11:5]", {}, 31, 25, kNumberColour},
                    rs2, rs1, funct3,
                    {"imm[4:0]", QString::number(instr.imm), 11, 7, kNumberColour},
                    opcode};

        case isa::InstrFormat::B:
            // The scrambled one. Naming the pieces is the point: bit 12 and
            // bit 11 do not sit where anyone expects.
            return {{"imm[12|10:5]", {}, 31, 25, kNumberColour},
                    rs2, rs1, funct3,
                    {"imm[4:1|11]", QString::number(instr.imm), 11, 7, kNumberColour},
                    opcode};

        case isa::InstrFormat::U:
            return {{"imm[31:12]", hex(static_cast<u32>(instr.imm) >> 12), 31, 12, kNumberColour},
                    rd, opcode};

        case isa::InstrFormat::J:
            return {{"imm[20|10:1|11|19:12]", QString::number(instr.imm), 31, 12, kNumberColour},
                    rd, opcode};

        // ---- compressed ----------------------------------------------------
        // The immediate labels are the spec's own scatter notation, which is
        // the point: c.lwsp, c.swsp and c.lw all encode a word offset and all
        // three put the bits somewhere different, and no amount of staring at
        // "imm" would ever show that.
        case isa::InstrFormat::CR:
            if (desc.syntax == isa::OperandSyntax::C_RS1) {
                return {{"funct4", hex(bits(word, 15, 12)), 15, 12, kDimColour}, crs1,
                        {"0", {}, 6, 2, kDimColour}, cop};
            }
            return {{"funct4", hex(bits(word, 15, 12)), 15, 12, kDimColour}, crd, crs2, cop};

        case isa::InstrFormat::CI:
            if (desc.syntax == isa::OperandSyntax::C_RD_SHAMT) {
                return {cfunct3, {"shamt[5]", {}, 12, 12, kNumberColour}, crd,
                        {"shamt[4:0]", QString::number(instr.imm), 6, 2, kNumberColour}, cop};
            }
            if (desc.syntax == isa::OperandSyntax::C_RD_OFS_SP) {
                return {cfunct3, {"off[5]", {}, 12, 12, kNumberColour}, crd,
                        {"off[4:2|7:6]", QString::number(instr.imm), 6, 2, kNumberColour}, cop};
            }
            if (desc.syntax == isa::OperandSyntax::C_IMM) {
                return {cfunct3, {"imm[9]", {}, 12, 12, kNumberColour}, crd,
                        {"imm[4|6|8:7|5]", QString::number(instr.imm), 6, 2, kNumberColour}, cop};
            }
            if (desc.syntax == isa::OperandSyntax::C_RD_UIMM) {
                return {cfunct3, {"imm[17]", {}, 12, 12, kNumberColour}, crd,
                        {"imm[16:12]", hex(static_cast<u32>(instr.imm) >> 12), 6, 2,
                         kNumberColour},
                        cop};
            }
            return {cfunct3, {"imm[5]", {}, 12, 12, kNumberColour}, crd,
                    {"imm[4:0]", QString::number(instr.imm), 6, 2, kNumberColour}, cop};

        case isa::InstrFormat::CSS:
            return {cfunct3, {"off[5:2|7:6]", QString::number(instr.imm), 12, 7, kNumberColour},
                    crs2, cop};

        case isa::InstrFormat::CIW:
            return {cfunct3,
                    {"imm[5:4|9:6|2|3]", QString::number(instr.imm), 12, 5, kNumberColour},
                    crdp_low, cop};

        case isa::InstrFormat::CL:
            return {cfunct3, {"off[5:3]", {}, 12, 10, kNumberColour}, crs1p,
                    {"off[2|6]", QString::number(instr.imm), 6, 5, kNumberColour}, crdp_low, cop};

        case isa::InstrFormat::CS:
            return {cfunct3, {"off[5:3]", {}, 12, 10, kNumberColour}, crs1p,
                    {"off[2|6]", QString::number(instr.imm), 6, 5, kNumberColour}, crs2p, cop};

        case isa::InstrFormat::CA:
            return {{"funct6", hex(bits(word, 15, 10)), 15, 10, kDimColour}, crdp,
                    {"funct2", hex(bits(word, 6, 5)), 6, 5, kDimColour}, crs2p, cop};

        case isa::InstrFormat::CB:
            // Shared by the branches and by c.srli/c.srai/c.andi, which reuse
            // the offset bits as a shift amount or an immediate.
            if (desc.syntax == isa::OperandSyntax::C_RS1P_LBL) {
                return {cfunct3, {"off[8|4:3]", {}, 12, 10, kNumberColour}, crs1p,
                        {"off[7:6|2:1|5]", QString::number(instr.imm), 6, 2, kNumberColour}, cop};
            }
            return {cfunct3, {"imm[5]", {}, 12, 12, kNumberColour},
                    {"funct2", hex(bits(word, 11, 10)), 11, 10, kDimColour}, crdp,
                    {"imm[4:0]", QString::number(instr.imm), 6, 2, kNumberColour}, cop};

        case isa::InstrFormat::CJ:
            return {cfunct3,
                    {"off[11|4|9:8|10|6|7|3:1|5]", QString::number(instr.imm), 12, 2,
                     kNumberColour},
                    cop};
    }
    return {opcode};
}

QString binary_of(u32 word, int hi, int lo) {
    QString out;
    for (int index = hi; index >= lo; --index) out += bit(word, index) != 0 ? '1' : '0';
    return out;
}

}  // namespace

EncodingWidget::EncodingWidget(const dbg::DebugSession& session, const dbg::Pipeline* pipeline,
                               QWidget* parent)
    : QWidget(parent), session_(&session), pipeline_(pipeline) {
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPointSize(10);
    setFont(font);
    setMinimumHeight(140);
}

void EncodingWidget::set_address(Addr addr) {
    address_ = addr;
    follow_pc_ = false;
    update();
}

void EncodingWidget::follow_pc() {
    follow_pc_ = true;
    update();
}

Addr EncodingWidget::shown_address() const {
    if (!follow_pc_) return address_;
    // The instruction in write-back is the one that just ran, and the one whose
    // effect this panel describes. The pc has moved on to what is still four
    // stages from retiring, so following it would explain the wrong line.
    if (pipeline_ != nullptr) {
        const dbg::Slot& retired = pipeline_->at(dbg::Stage::WB);
        if (retired.valid) return retired.pc;
    }
    return session_->cpu().pc;
}

QSize EncodingWidget::sizeHint() const { return QSize(600, 160); }

QStringList EncodingWidget::field_summary() const {
    const isa::DecodedInstr instr = isa::decode(session_->read_imem_instr(shown_address()));
    QStringList out;
    if (!instr.valid()) return out;
    for (const BitField& field : fields_of(instr)) {
        out << (field.detail.isEmpty() ? field.label : field.label + "=" + field.detail);
    }
    return out;
}

void EncodingWidget::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    painter.fillRect(rect(), QColor(0x1e, 0x21, 0x27));

    const Addr address = shown_address();
    const Word word = session_->read_imem_instr(address);
    const isa::DecodedInstr instr = isa::decode(word);

    const QFontMetrics metrics(font());
    const int line_height = metrics.height() + 2;
    const int char_width = metrics.horizontalAdvance('0');
    int y = 6;

    // Heading: where, what bits, and what it disassembles to.
    {
        // Show the bits at the width they occupy: eight digits for a 32-bit
        // instruction, four for a compressed one.
        const QString bits = instr.length == 2
                                 ? QString::asprintf("    %04x", word & 0xffffu)
                                 : QString::asprintf("%08x", word);
        const QString text = QString::asprintf("0x%08x   ", address) + bits + "   " +
                             QString::fromStdString(isa::disassemble_word(word, address));
        painter.setPen(kTextColour);
        painter.drawText(8, y + metrics.ascent(), text);
        y += line_height + 4;
    }

    if (!instr.valid()) {
        painter.setPen(QColor(0xf2, 0x6d, 0x6d));
        painter.drawText(8, y + metrics.ascent(), "not a valid instruction encoding");
        return;
    }

    const QList<BitField> fields = fields_of(instr);

    // Each field is at least five columns wide, so a range label like "14 12"
    // fits even on a three-bit field.
    QList<int> widths;
    int total = 0;
    for (const BitField& field : fields) {
        const int columns = std::max(field.bits(), 5) + 2;
        widths.append(columns * char_width);
        total += widths.back();
    }

    // The boxes: bit range on top, the bits themselves below.
    int x = 8;
    const int box_height = line_height * 2 + 6;
    painter.setPen(kFrameColour);
    painter.drawRect(x, y, total, box_height);

    int cursor = x;
    for (int index = 0; index < fields.size(); ++index) {
        const BitField& field = fields[index];
        const QRect cell(cursor, y, widths[index], box_height);

        if (index > 0) {
            painter.setPen(kFrameColour);
            painter.drawLine(cursor, y, cursor, y + box_height);
        }

        painter.setPen(kDimColour);
        const QString range = field.hi == field.lo
                                  ? QString::number(field.hi)
                                  : QString::number(field.hi) + "  " + QString::number(field.lo);
        painter.drawText(cell.adjusted(2, 3, -2, -box_height / 2), Qt::AlignCenter, range);

        painter.setPen(field.colour);
        painter.drawText(cell.adjusted(2, box_height / 2, -2, -3), Qt::AlignCenter,
                         binary_of(word, field.hi, field.lo));

        cursor += widths[index];
    }
    y += box_height + 8;

    // What each field means, wrapped across as many lines as it takes.
    int column = 8;
    for (const BitField& field : fields) {
        const QString text =
            field.detail.isEmpty() ? field.label : field.label + "=" + field.detail;
        const int text_width = metrics.horizontalAdvance(text);
        if (column + text_width > width() - 8) {
            column = 8;
            y += line_height;
        }
        painter.setPen(field.colour);
        painter.drawText(column, y + metrics.ascent(), text);
        column += text_width + 3 * char_width;
    }
    y += line_height + 4;

    // For a compressed instruction, the base instruction it stands for. This is
    // the lesson: every C-extension encoding is a shorter spelling of something
    // RV32I could already say, and seeing the two side by side is what makes
    // "c.addi a0, 1" stop looking like a new instruction.
    if (isa::is_compressed_format(instr.desc().format)) {
        const isa::DecodedInstr base = isa::expand(instr);
        const isa::EncodeResult encoded = isa::encode(base.id, isa::operands_of(base));
        QString text = "expands to  " +
                       QString::fromStdString(isa::disassemble(base, address));
        if (encoded.ok()) text += QString::asprintf("   (%08x)", encoded.word);
        painter.setPen(QColor(0x8a, 0xb4, 0xf8));
        painter.drawText(8, y + metrics.ascent(), text);
        y += line_height + 4;
    }

    // What it did, or where it came from. This is the line that connects the
    // bits to the machine.
    const core::StepOutcome& last = session_->last_step();
    QString effect;
    QColor effect_colour = kDimColour;

    if (last.pc_before == address && last.reg_written != core::kNoReg) {
        effect = QString::asprintf(
            "→ %s = 0x%08x   (was 0x%08x)", isa::abi_name(last.reg_written).data(),
            session_->cpu().x[last.reg_written], last.reg_old);
        effect_colour = QColor(0xe8, 0xc0, 0x4e);
    } else if (last.pc_before == address && last.mem_written != core::kNoAddr) {
        effect = QString::asprintf("→ [0x%08x] = 0x%08x   (was 0x%08x)", last.mem_written,
                                   session_->read_dmem_word(last.mem_written), last.mem_old);
        effect_colour = QColor(0xe8, 0xc0, 0x4e);
    } else if (const auto entry = session_->map().at(address)) {
        effect = entry->count > 1
                     ? QString::asprintf("from line %u, word %u of %u", entry->line,
                                         entry->slot + 1, entry->count)
                     : QString::asprintf("from line %u", entry->line);
    }

    if (!effect.isEmpty()) {
        painter.setPen(effect_colour);
        painter.drawText(8, y + metrics.ascent(), effect);
    }
}

}  // namespace rv::gui
