#include "gui/pipeline_widget.hpp"

#include <QFontDatabase>
#include <QPainter>
#include <QStringList>

#include <algorithm>
#include <map>
#include <vector>

namespace rv::gui {
namespace {

using dbg::CycleRecord;
using dbg::kNumStages;
using dbg::Slot;
using dbg::Stage;

// The palette the rest of the GUI already draws from, one entry per stage. The
// stage keeps its colour; the instruction moving through it does not. Which
// instruction is where is answered by the words in the boxes and by the
// diagram underneath -- the colour is here to make the five stages nameable at
// a glance.
const QColor kStageColour[kNumStages] = {
    QColor(0x6c, 0xb6, 0xff),  // IF  -- mnemonic blue
    QColor(0x5b, 0xd6, 0xc8),  // ID  -- register teal
    QColor(0x89, 0xd1, 0x85),  // EX  -- label green
    QColor(0xe8, 0xa0, 0x4e),  // MEM -- number amber
    QColor(0xc5, 0x92, 0xdd),  // WB  -- directive purple
};

const QColor kTextColour(0xd6, 0xd6, 0xd6);
const QColor kDimColour(0x7d, 0x85, 0x90);
const QColor kFrameColour(0x45, 0x4b, 0x56);
const QColor kErrorColour(0xf2, 0x6d, 0x6d);
const QColor kBackground(0x1e, 0x21, 0x27);

constexpr std::size_t index_of(Stage stage) { return static_cast<std::size_t>(stage); }

/// How many cycles the space-time diagram looks back. Wide enough to hold a
/// whole instruction's five stages twice over, which is as far back as anything
/// on screen is still being reasoned about -- filling the panel's full width
/// just pushed the interesting end of the run away from the eye.
constexpr int kDiagramCycles = 10;

constexpr Stage kStages[kNumStages] = {Stage::IF, Stage::ID, Stage::EX, Stage::MEM, Stage::WB};

QString spelling(const dbg::Pipeline& pipeline, Stage stage) {
    const std::string_view name = pipeline.mnemonic(stage);
    return QString::fromUtf8(name.data(), static_cast<int>(name.size()));
}

QString short_pc(Addr pc) { return QString::asprintf("%04x", pc & 0xffffu); }

/// The colour a slot's text is drawn in: its stage's, unless the word does not
/// decode, which is worth shouting about wherever it appears.
QColor slot_colour(const Slot& slot, Stage stage) {
    if (!slot.valid) return kDimColour;
    if (!slot.instr.valid()) return kErrorColour;
    return kStageColour[index_of(stage)];
}

}  // namespace

PipelineWidget::PipelineWidget(const dbg::Pipeline& pipeline, const dbg::DebugSession& session,
                               QWidget* parent)
    : QWidget(parent), pipeline_(&pipeline), session_(&session) {
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPointSize(10);
    setFont(font);
    setMinimumHeight(220);
}

QSize PipelineWidget::sizeHint() const { return QSize(600, 260); }

QStringList PipelineWidget::stage_summary() const {
    QStringList out;
    for (const Stage stage : kStages) {
        const Slot& slot = pipeline_->at(stage);
        const QString name = QString::fromUtf8(dbg::stage_name(stage));
        if (!slot.valid) {
            out << name + "=-";
            continue;
        }
        out << name + "=" + spelling(*pipeline_, stage) + "@" + short_pc(slot.pc);
    }
    return out;
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

void PipelineWidget::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    painter.fillRect(rect(), kBackground);

    const QFontMetrics metrics(font());
    const int line_height = metrics.height() + 2;
    int y = 6;

    // Heading: what this is, and how many cycles it has been running. The count
    // is the pipeline's own -- the status bar carries the architectural one.
    {
        painter.setPen(kTextColour);
        painter.drawText(8, y + metrics.ascent(), "Pipeline");

        QString right = QString::asprintf("cycle %llu",
                                          static_cast<unsigned long long>(pipeline_->ticks()));
        if (pipeline_->last_flushed() > 0) {
            right = QString::asprintf("flushed %u   ", pipeline_->last_flushed()) + right;
        }
        if (pipeline_->last_mispredicted()) right = "mispredicted   " + right;
        painter.setPen(pipeline_->last_mispredicted() ? QColor(0xf2, 0x6d, 0x6d) : kDimColour);
        painter.drawText(QRect(8, y, width() - 16, line_height),
                         Qt::AlignRight | Qt::AlignVCenter, right);
        y += line_height + 4;
    }

    y = draw_strip(painter, y);
    draw_diagram(painter, y + 6);
}

int PipelineWidget::draw_strip(QPainter& painter, int y) {
    const QFontMetrics metrics(font());
    const int line_height = metrics.height() + 2;
    const int char_width = metrics.horizontalAdvance('0');

    // Wide enough for the longest thing any cell has to say, so the five boxes
    // stay the same size as the instructions in them change.
    int columns = 7;
    for (const Stage stage : kStages) {
        columns = std::max(columns, static_cast<int>(spelling(*pipeline_, stage).size()) + 1);
    }
    const int cell_width = (columns + 2) * char_width;
    const int box_height = line_height * 3 + 8;
    const int left = 8;

    painter.setPen(kFrameColour);
    painter.drawRect(left, y, cell_width * static_cast<int>(kNumStages), box_height);

    for (std::size_t index = 0; index < kNumStages; ++index) {
        const Stage stage = kStages[index];
        const Slot& slot = pipeline_->at(stage);
        const QRect cell(left + static_cast<int>(index) * cell_width, y, cell_width, box_height);

        if (index > 0) {
            painter.setPen(kFrameColour);
            painter.drawLine(cell.left(), y, cell.left(), y + box_height);
        }

        // The stage's name, then what is in it, then where that came from.
        painter.setPen(kStageColour[index_of(stage)]);
        painter.drawText(QRect(cell.left(), y + 3, cell.width(), line_height),
                         Qt::AlignCenter, dbg::stage_name(stage));

        painter.setPen(slot_colour(slot, stage));
        const QString name = slot.valid ? spelling(*pipeline_, stage) : QString("--");
        painter.drawText(QRect(cell.left(), y + 3 + line_height, cell.width(), line_height),
                         Qt::AlignCenter, name);

        if (slot.valid) {
            painter.setPen(kDimColour);
            painter.drawText(QRect(cell.left(), y + 3 + line_height * 2, cell.width(), line_height),
                             Qt::AlignCenter, short_pc(slot.pc));
        }
    }

    // Write-back is the stage that is not decoration: this is where an
    // instruction becomes part of the machine.
    const int strip_width = cell_width * static_cast<int>(kNumStages);
    const QRect wb(left + static_cast<int>(index_of(Stage::WB)) * cell_width, y, cell_width,
                   box_height);
    painter.setPen(kStageColour[index_of(Stage::WB)]);
    painter.drawRect(wb.left(), wb.top(), wb.width(), wb.height());
    painter.setPen(kDimColour);
    painter.drawText(QRect(left, y + box_height + 2, strip_width, line_height),
                     Qt::AlignRight | Qt::AlignVCenter, "the register file is written here ↑");
    y += box_height + line_height + 4;

    // What each stage is for, because a five-letter abbreviation does not say
    // it and this is a teaching tool.
    painter.setPen(kDimColour);
    painter.drawText(QRect(left, y, width() - left - 8, line_height),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     "IF fetch · ID decode · EX ALU · MEM data memory · WB register write");
    y += line_height;
    painter.drawText(QRect(left, y, width() - left - 8, line_height),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     "branch target computed in EX · guessed taken · checked at WB");

    return y + line_height + 4;
}

void PipelineWidget::draw_diagram(QPainter& painter, int y) {
    const QFontMetrics metrics(font());
    const int line_height = metrics.height() + 2;
    const int char_width = metrics.horizontalAdvance('0');
    const int left = 8;

    // A label wide enough to tell two passes of the same loop apart, then three
    // columns per cycle: two for the stage, one for the gap.
    const int label_width = 15 * char_width;
    // Two columns for the stage, plus room for the cycle number above it once
    // the count grows past two digits.
    int digits = 2;
    for (u64 ticks = pipeline_->ticks(); ticks >= 100; ticks /= 10) ++digits;
    const int column_width = (digits + 1) * char_width;
    const int room = width() - left - label_width - 8;
    if (room < column_width) return;

    const int max_columns = std::min(kDiagramCycles, room / column_width);
    const int max_rows = std::max(1, (height() - y - line_height) / line_height - 1);

    // The cycles that fit, newest last. The first record is where the pipe
    // started rather than a cycle that ran, so it has nothing to draw.
    std::vector<const CycleRecord*> columns;
    for (const CycleRecord& entry : pipeline_->history()) {
        if (entry.tick > 0) columns.push_back(&entry);
    }
    if (columns.empty()) return;
    if (static_cast<int>(columns.size()) > max_columns) {
        columns.erase(columns.begin(),
                      columns.end() - max_columns);
    }

    // One row per instruction seen in those cycles, in the order they were
    // fetched. A flushed instruction simply stops partway across, which is the
    // clearest thing the diagram says.
    struct Row {
        QString label;
        std::map<u64, Stage> cells;
    };
    std::map<u64, Row> rows;
    for (const CycleRecord* entry : columns) {
        for (std::size_t index = 0; index < kNumStages; ++index) {
            const Slot& slot = entry->stages[index];
            if (!slot.valid || slot.serial == 0) continue;
            Row& row = rows[slot.serial];
            if (row.label.isEmpty()) {
                const std::string_view name =
                    slot.instr.valid() ? slot.instr.desc().mnemonic : std::string_view("??");
                row.label = QString::asprintf(
                    "%-8s %s",
                    std::string(name).c_str(),
                    short_pc(slot.pc).toUtf8().constData());
            }
            row.cells[entry->tick] = kStages[index];
        }
    }
    if (rows.empty()) return;

    // Keep the newest rows when there are more than there is room for: the
    // interesting end of a long run is the recent end.
    while (static_cast<int>(rows.size()) > max_rows) rows.erase(rows.begin());

    // The cycle numbers along the top, named once on the left rather than
    // prefixed onto every column.
    painter.setPen(kDimColour);
    painter.drawText(QRect(left, y, label_width, line_height),
                     Qt::AlignLeft | Qt::AlignVCenter, "cycle");
    for (std::size_t column = 0; column < columns.size(); ++column) {
        painter.drawText(
            QRect(left + label_width + static_cast<int>(column) * column_width, y, column_width,
                  line_height),
            Qt::AlignLeft | Qt::AlignVCenter,
            QString::number(static_cast<unsigned long long>(columns[column]->tick)));
    }
    y += line_height;

    for (const auto& [serial, row] : rows) {
        painter.setPen(kTextColour);
        painter.drawText(QRect(left, y, label_width, line_height),
                         Qt::AlignLeft | Qt::AlignVCenter, row.label);

        for (std::size_t column = 0; column < columns.size(); ++column) {
            const auto found = row.cells.find(columns[column]->tick);
            if (found == row.cells.end()) continue;
            painter.setPen(kStageColour[index_of(found->second)]);
            painter.drawText(QRect(left + label_width + static_cast<int>(column) * column_width, y,
                                   column_width, line_height),
                             Qt::AlignLeft | Qt::AlignVCenter,
                             dbg::stage_abbrev(found->second));
        }
        y += line_height;
    }
}

}  // namespace rv::gui
