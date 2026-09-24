#include "gui/editor_widget.hpp"

#include <QFontDatabase>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>

#include <algorithm>
#include <QTextBlock>

#include "gui/highlighter.hpp"

namespace rv::gui {
namespace {

constexpr int kMarkerColumns = 2;  // breakpoint dot, execution arrow
constexpr int kGutterPadding = 8;

/// How far along the last stage is. Its line is the one that gets a colour.
constexpr int kLastStage = 4;
constexpr int kStageColumns = 3;  // two letters and a space before the code

const char* stage_label(int depth) {
    static const char* const kNames[] = {"IF", "ID", "EX", "ME", "WB"};
    return kNames[std::clamp(depth, 0, kLastStage)];
}

/// The panel's colours, so a name in the margin and a box in the panel are
/// recognisably the same stage.
QColor stage_ink(int depth) {
    static const QColor kInk[] = {
        QColor(0x6c, 0xb6, 0xff),  // IF
        QColor(0x5b, 0xd6, 0xc8),  // ID
        QColor(0x89, 0xd1, 0x85),  // EX
        QColor(0xe8, 0xa0, 0x4e),  // MEM
        QColor(0xc5, 0x92, 0xdd),  // WB
    };
    return kInk[std::clamp(depth, 0, kLastStage)];
}

/// Only two things earn a background across the whole line.
///
/// The line that has just finished -- write-back -- in yellow, so there is
/// exactly one of them at a time and nothing to measure. Five shades of one
/// hue asked the eye to rank brightness, which it is bad at; the stage names
/// in the margin say the same thing without the guessing.
///
/// And a branch that has resolved, in green or red, because which way it went
/// is the one thing here worth seeing from across the room. An invalid colour
/// means the line is named in the margin and otherwise left alone.
QColor line_tint(const PipelineMark& mark) {
    switch (mark.tone) {
        case PipelineTone::Taken: return QColor(0x38, 0x79, 0x51);
        case PipelineTone::NotTaken: return QColor(0x7d, 0x40, 0x44);
        case PipelineTone::Pending: break;
    }
    return mark.depth == kLastStage ? QColor(0x78, 0x6e, 0x35) : QColor();
}

QColor colour_for(as::Severity severity) {
    switch (severity) {
        case as::Severity::Error: return QColor(0xf2, 0x6d, 0x6d);
        case as::Severity::Warning: return QColor(0xe8, 0xc0, 0x4e);
        case as::Severity::Note:
        case as::Severity::Help: return QColor(0x5b, 0xd6, 0xc8);
    }
    return QColor(0xd6, 0xd6, 0xd6);
}

}  // namespace

// ---------------------------------------------------------------------------
// Suggestion bar
// ---------------------------------------------------------------------------

namespace {

constexpr int kBarPadding = 6;
constexpr int kItemPadding = 7;
constexpr int kItemGap = 4;
/// How many candidates the block shows before it says "+N" instead. A block
/// under the caret is a nudge, not a catalogue.
constexpr std::size_t kMaxShown = 8;

}  // namespace

SuggestionBar::SuggestionBar(QWidget* parent) : QWidget(parent) {
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPointSize(10);
    setFont(font);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    hide();
}

int SuggestionBar::content_width() const {
    const QFontMetrics metrics(font());
    int width_of = 2 * kBarPadding;
    const std::size_t shown = std::min(candidates_.size(), kMaxShown);
    for (std::size_t index = 0; index < shown; ++index) {
        width_of += metrics.horizontalAdvance(QString::fromStdString(candidates_[index].text)) +
                    2 * kItemPadding + kItemGap;
    }
    if (shown < candidates_.size()) {
        width_of += metrics.horizontalAdvance(
                        QStringLiteral("+%1").arg(candidates_.size() - shown)) +
                    2 * kItemPadding;
    }
    return width_of;
}

QSize SuggestionBar::sizeHint() const {
    return QSize(content_width(), fontMetrics().height() + 8);
}

void SuggestionBar::show_for(std::vector<as::Completion> candidates) {
    candidates_ = std::move(candidates);
    current_ = 0;
    setVisible(!candidates_.empty());
    update();
}

void SuggestionBar::fold() {
    candidates_.clear();
    current_ = 0;
    hide();
}

void SuggestionBar::advance(int delta) {
    if (candidates_.empty()) return;
    const int count = static_cast<int>(candidates_.size());
    current_ = ((current_ + delta) % count + count) % count;
    update();
}

const as::Completion* SuggestionBar::highlighted() const {
    if (candidates_.empty()) return nullptr;
    return &candidates_[static_cast<std::size_t>(current_)];
}

QStringList SuggestionBar::entries() const {
    QStringList out;
    out.reserve(static_cast<int>(candidates_.size()));
    for (const as::Completion& candidate : candidates_) {
        out << QString::fromStdString(candidate.text);
    }
    return out;
}

void SuggestionBar::paintEvent(QPaintEvent* /*event*/) {
    if (candidates_.empty()) return;

    QPainter painter(this);
    painter.setFont(font());
    painter.setRenderHint(QPainter::Antialiasing);

    // A border, because this floats over the code rather than sitting in a
    // reserved strip -- without one it would read as part of the program.
    const QRectF frame = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    painter.setPen(QColor(0x39, 0x40, 0x4b));
    painter.setBrush(QColor(0x21, 0x25, 0x2c));
    painter.drawRoundedRect(frame, 4, 4);

    const QFontMetrics metrics(font());
    const int baseline = (height() + metrics.ascent() - metrics.descent()) / 2;
    int x = kBarPadding;
    const std::size_t shown = std::min(candidates_.size(), kMaxShown);

    for (std::size_t index = 0; index < shown; ++index) {
        const QString text = QString::fromStdString(candidates_[index].text);
        const int width_of = metrics.horizontalAdvance(text) + 2 * kItemPadding;

        if (index == static_cast<std::size_t>(current_)) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(0x2f, 0x4b, 0x6e));
            painter.drawRoundedRect(QRectF(x, 3, width_of, height() - 6), 3, 3);
            painter.setPen(QColor(0xe6, 0xed, 0xf3));
        } else {
            painter.setPen(QColor(0x8b, 0x94, 0xa1));
        }
        painter.drawText(x + kItemPadding, baseline, text);
        x += width_of + kItemGap;
    }

    // Never quietly drop the rest -- say how many are not being shown.
    if (shown < candidates_.size()) {
        painter.setPen(QColor(0x5f, 0x67, 0x73));
        painter.drawText(x + kItemPadding, baseline,
                         QStringLiteral("+%1").arg(candidates_.size() - shown));
    }
}

// ---------------------------------------------------------------------------
// Gutter
// ---------------------------------------------------------------------------

GutterWidget::GutterWidget(EditorWidget* editor) : QWidget(editor), editor_(editor) {}

QSize GutterWidget::sizeHint() const { return QSize(editor_->gutter_width(), 0); }

void GutterWidget::paintEvent(QPaintEvent* event) { editor_->paint_gutter(event); }

void GutterWidget::mousePressEvent(QMouseEvent* event) {
    // Clicking the margin toggles a breakpoint, which is the one gesture
    // everybody already knows from every other editor.
    const int line = editor_->line_at(static_cast<int>(event->position().y()));
    if (line > 0) emit editor_->breakpointToggled(line);
}

// ---------------------------------------------------------------------------
// Editor
// ---------------------------------------------------------------------------

EditorWidget::EditorWidget(QWidget* parent) : QPlainTextEdit(parent), gutter_(new GutterWidget(this)) {
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPointSize(11);
    setFont(font);
    // Assembly is column-sensitive to read, so tabs must not vary in width.
    setTabStopDistance(4 * QFontMetricsF(font).horizontalAdvance(' '));
    setLineWrapMode(QPlainTextEdit::NoWrap);

    new AsmHighlighter(document());

    // A small block under the caret, which is where the eye already is. It
    // floats over the text rather than claiming a row, so opening it never
    // moves the line being written.
    bar_ = new SuggestionBar(this);
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, &EditorWidget::update_ghost);
    connect(this, &QPlainTextEdit::textChanged, this, &EditorWidget::update_ghost);
    // The block is pinned to the caret, so it has to follow it -- both when it
    // moves and when the view scrolls under it.
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, &EditorWidget::place_bar);

    connect(this, &QPlainTextEdit::blockCountChanged, this, &EditorWidget::update_gutter_width);
    connect(this, &QPlainTextEdit::updateRequest, this, &EditorWidget::update_gutter);
    connect(this, &QPlainTextEdit::cursorPositionChanged, this,
            &EditorWidget::highlight_current_line);

    update_gutter_width();
    highlight_current_line();
}

int EditorWidget::gutter_width() const {
    int digits = 2;
    for (int lines = blockCount(); lines >= 100; lines /= 10) ++digits;
    const int digit_width = fontMetrics().horizontalAdvance(QLatin1Char('9'));
    const int marker_width = fontMetrics().horizontalAdvance(QLatin1Char('M'));
    return kGutterPadding + kMarkerColumns * marker_width + digits * digit_width +
           kStageColumns * digit_width + kGutterPadding;
}

void EditorWidget::update_gutter_width() { setViewportMargins(gutter_width(), 0, 0, 0); }

void EditorWidget::update_gutter(const QRect& rect, int dy) {
    if (dy != 0) {
        gutter_->scroll(0, dy);
    } else {
        gutter_->update(0, rect.y(), gutter_->width(), rect.height());
    }
    if (rect.contains(viewport()->rect())) update_gutter_width();
    place_bar();
}

void EditorWidget::resizeEvent(QResizeEvent* event) {
    QPlainTextEdit::resizeEvent(event);
    const QRect area = contentsRect();
    gutter_->setGeometry(QRect(area.left(), area.top(), gutter_width(), area.height()));
    place_bar();
}

void EditorWidget::place_bar() {
    if (!bar_->is_open()) return;

    // Under the word being typed, on the next line down, so it points at what
    // it is about without covering it.
    const QRect caret = cursorRect();
    const int height_of = bar_->sizeHint().height();
    const int width_of = std::min(bar_->content_width(), viewport()->width());

    int x = viewport()->x() + caret.left();
    x = std::min(x, viewport()->x() + viewport()->width() - width_of);
    x = std::max(x, viewport()->x());

    // Below the line normally; above it when there is no room, which is what
    // happens on the last line of a full screen.
    int y = viewport()->y() + caret.bottom() + 3;
    if (y + height_of > viewport()->y() + viewport()->height()) {
        y = viewport()->y() + caret.top() - height_of - 3;
    }

    bar_->setGeometry(QRect(x, y, width_of, height_of));
    bar_->raise();
}

// ---------------------------------------------------------------------------
// Completion
// ---------------------------------------------------------------------------

QStringList EditorWidget::visible_completions() const { return bar_->entries(); }

void EditorWidget::update_ghost() {
    const QTextCursor cursor = textCursor();
    const QByteArray line = cursor.block().text().toUtf8();
    const QString ghost = QString::fromStdString(as::ghost_text(
        std::string_view(line.constData(), static_cast<std::size_t>(line.size())),
        static_cast<u16>(cursor.positionInBlock())));
    if (ghost == ghost_) return;
    ghost_ = ghost;
    viewport()->update();
}

void EditorWidget::paintEvent(QPaintEvent* event) {
    QPlainTextEdit::paintEvent(event);
    if (ghost_.isEmpty() || !hasFocus()) return;

    // Drawn straight after the caret, in the colour of something that is not
    // there yet: it reads as the rest of the line waiting to be typed, and it
    // shortens as the operands get written.
    QPainter painter(viewport());
    painter.setFont(font());
    painter.setPen(QColor(0x55, 0x5c, 0x69));
    const QRect caret = cursorRect();
    painter.drawText(caret.left() + 1, caret.top() + fontMetrics().ascent(), ghost_);
}

void EditorWidget::request_completion() {
    const QTextCursor cursor = textCursor();
    const QByteArray line = cursor.block().text().toUtf8();
    const std::string_view view(line.constData(), static_cast<std::size_t>(line.size()));
    const auto column = static_cast<u16>(cursor.positionInBlock());
    const as::Context context = as::context_at(view, column);

    // The bar is for choosing the instruction and nothing else. Once the
    // mnemonic is settled the operands answer for themselves, in the line,
    // as ghost text -- so a second list here would be a second answer to a
    // question already answered.
    if (context.where != as::Context::Where::Mnemonic) {
        close_bar();
        return;
    }

    std::vector<as::Completion> candidates = as::complete(view, column, symbols_);
    if (candidates.empty()) {
        close_bar();
        return;
    }

    typed_prefix_ = QString::fromStdString(context.prefix);
    previewing_ = false;
    bar_->show_for(std::move(candidates));
    place_bar();
}

void EditorWidget::close_bar() {
    if (!bar_->is_open()) return;
    bar_->fold();
    previewing_ = false;
}

void EditorWidget::cycle_suggestion(int delta) {
    if (!bar_->is_open()) return;

    // The first Tab takes the best match; the ones after it walk along the
    // bar. Each step lands in the buffer rather than only in the bar, so the
    // choice is read in the code being written and not off to the side.
    if (previewing_) {
        bar_->advance(delta);
    }
    previewing_ = true;
    if (const as::Completion* choice = bar_->highlighted(); choice != nullptr) {
        insert_completion(QString::fromStdString(choice->text), /*settle=*/false);
    }
}

void EditorWidget::insert_completion(const QString& text, bool settle) {
    const QTextCursor at = textCursor();
    const QByteArray line = at.block().text().toUtf8();
    const as::Context context = as::context_at(
        std::string_view(line.constData(), static_cast<std::size_t>(line.size())),
        static_cast<u16>(at.positionInBlock()));

    // Settling leaves the caret one space along, which is where the operands
    // are: choosing the instruction and starting to fill it in become the same
    // keystroke, and the ghost appears without a further one.
    const bool takes_operands = !as::operand_hints(text.toStdString()).empty();
    const QString inserted = settle && takes_operands ? text + QLatin1Char(' ') : text;

    inserting_ = true;
    QTextCursor cursor = textCursor();
    cursor.setPosition(at.block().position() + context.prefix_begin);
    cursor.setPosition(at.position(), QTextCursor::KeepAnchor);
    cursor.insertText(inserted);
    setTextCursor(cursor);
    inserting_ = false;

    if (settle) close_bar();
    update_ghost();
}

void EditorWidget::keyPressEvent(QKeyEvent* event) {
    if (bar_->is_open()) {
        // While the bar is up these keys are its own.
        switch (event->key()) {
            case Qt::Key_Tab:
                cycle_suggestion(+1);
                return;
            case Qt::Key_Backtab:
                cycle_suggestion(-1);
                return;
            case Qt::Key_Enter:
            case Qt::Key_Return:
                if (const as::Completion* choice = bar_->highlighted(); choice != nullptr) {
                    insert_completion(QString::fromStdString(choice->text));
                    return;
                }
                break;
            case Qt::Key_Escape:
                close_bar();
                return;
            default:
                break;
        }
    }

    const bool explicit_request =
        event->key() == Qt::Key_Space && (event->modifiers() & Qt::ControlModifier) != 0;
    if (explicit_request) {
        request_completion();
        return;
    }

    QPlainTextEdit::keyPressEvent(event);

    // Offer suggestions once there is enough of a word to narrow them, and keep
    // them current while it grows. Fewer than two characters would put a list
    // of everything in front of someone who has not asked for one.
    const QTextCursor cursor = textCursor();
    const QByteArray line = cursor.block().text().toUtf8();
    const as::Context context = as::context_at(
        std::string_view(line.constData(), static_cast<std::size_t>(line.size())),
        static_cast<u16>(cursor.positionInBlock()));

    if (context.where == as::Context::Where::Mnemonic && context.prefix.size() >= 2) {
        request_completion();
    } else {
        close_bar();
    }
}

int EditorWidget::line_at(int y) const {
    QTextBlock block = firstVisibleBlock();
    int top = static_cast<int>(blockBoundingGeometry(block).translated(contentOffset()).top());

    while (block.isValid()) {
        const int bottom = top + static_cast<int>(blockBoundingRect(block).height());
        if (y >= top && y <= bottom) return block.blockNumber() + 1;
        block = block.next();
        top = bottom;
    }
    return 0;
}

void EditorWidget::paint_gutter(QPaintEvent* event) {
    QPainter painter(gutter_);
    painter.fillRect(event->rect(), QColor(0x1e, 0x21, 0x27));

    QTextBlock block = firstVisibleBlock();
    int top = static_cast<int>(blockBoundingGeometry(block).translated(contentOffset()).top());
    const int marker_width = fontMetrics().horizontalAdvance(QLatin1Char('M'));

    while (block.isValid() && top <= event->rect().bottom()) {
        const int bottom = top + static_cast<int>(blockBoundingRect(block).height());
        const int line = block.blockNumber() + 1;

        if (block.isVisible() && bottom >= event->rect().top()) {
            const int height = static_cast<int>(blockBoundingRect(block).height());
            int x = kGutterPadding;

            // Marker precedence matches the terminal version: a breakpoint is a
            // decision the user made, so it outranks a diagnostic the tool
            // inferred.
            const bool has_breakpoint =
                std::find(breakpoints_.begin(), breakpoints_.end(), line) != breakpoints_.end();
            const auto diagnostic = diagnostics_.find(line);

            if (has_breakpoint) {
                painter.setBrush(QColor(0xf2, 0x6d, 0x6d));
                painter.setPen(Qt::NoPen);
                const int radius = std::min(height, marker_width) / 3;
                painter.drawEllipse(QPoint(x + marker_width / 2, top + height / 2), radius, radius);
            } else if (diagnostic != diagnostics_.end()) {
                painter.setPen(colour_for(diagnostic->second.severity));
                painter.drawText(QRect(x, top, marker_width, height), Qt::AlignCenter,
                                 diagnostic->second.severity == as::Severity::Error ? "E" : "W");
            } else if (std::find(overflow_.begin(), overflow_.end(), line) != overflow_.end()) {
                painter.setPen(QColor(0xf2, 0x6d, 0x6d));
                painter.drawText(QRect(x, top, marker_width, height), Qt::AlignCenter, "▮");
            }
            x += marker_width;

            if (line == execution_line_) {
                painter.setPen(QColor(0xe8, 0xc0, 0x4e));
                painter.drawText(QRect(x, top, marker_width, height), Qt::AlignCenter, "▶");
            }
            x += marker_width;

            const int stage_width =
                kStageColumns * fontMetrics().horizontalAdvance(QLatin1Char('9'));

            painter.setPen(line == current_line() ? QColor(0xd6, 0xd6, 0xd6)
                                                  : QColor(0x6a, 0x71, 0x7c));
            painter.drawText(
                QRect(x, top, gutter_->width() - x - kGutterPadding - stage_width, height),
                Qt::AlignRight | Qt::AlignVCenter, QString::number(line));

            // Which stage this line's instruction is in, named rather than
            // shaded, in the colour the panel gives that stage.
            if (const PipelineMark* mark = pipeline_mark(line); mark != nullptr) {
                painter.setPen(stage_ink(mark->depth));
                painter.drawText(
                    QRect(gutter_->width() - kGutterPadding - stage_width, top, stage_width,
                          height),
                    Qt::AlignRight | Qt::AlignVCenter, stage_label(mark->depth));
            }
        }

        block = block.next();
        top = bottom;
    }
}

void EditorWidget::highlight_current_line() {
    QList<QTextEdit::ExtraSelection> selections;

    QTextEdit::ExtraSelection line;
    line.format.setBackground(QColor(0x26, 0x2a, 0x33));
    line.format.setProperty(QTextFormat::FullWidthSelection, true);
    line.cursor = textCursor();
    line.cursor.clearSelection();
    selections.append(line);

    // The two lines that get a colour: the one finishing, and a branch that has
    // resolved. Before the overflow marks, so a line that is both in the pipe
    // and past the end of imem still shows the red that matters more.
    for (const PipelineMark& mark : pipeline_) {
        const QColor colour = line_tint(mark);
        if (!colour.isValid()) continue;
        QTextBlock block = document()->findBlockByNumber(mark.line - 1);
        if (!block.isValid()) continue;
        QTextEdit::ExtraSelection tint;
        tint.format.setBackground(colour);
        tint.format.setProperty(QTextFormat::FullWidthSelection, true);
        tint.cursor = QTextCursor(block);
        tint.cursor.clearSelection();
        selections.append(tint);
    }

    // Lines that fall past the end of the memory they belong in, in red
    // across the whole width. Not an error -- what is written is valid, there
    // is simply nowhere to put it -- so it marks rather than stops, and a
    // bigger memory or a shorter program makes it go away.
    for (const int number : overflow_) {
        QTextBlock block = document()->findBlockByNumber(number - 1);
        if (!block.isValid()) continue;
        QTextEdit::ExtraSelection mark;
        mark.format.setBackground(QColor(0x4a, 0x1f, 0x25));
        mark.format.setProperty(QTextFormat::FullWidthSelection, true);
        mark.cursor = QTextCursor(block);
        mark.cursor.clearSelection();
        selections.append(mark);
    }

    // The span each diagnostic points at, restyled rather than covered -- the
    // offending word has to stay readable.
    for (const auto& [number, diagnostic] : diagnostics_) {
        QTextBlock block = document()->findBlockByNumber(number - 1);
        if (!block.isValid()) continue;

        QTextEdit::ExtraSelection mark;
        mark.format.setUnderlineStyle(QTextCharFormat::WaveUnderline);
        mark.format.setUnderlineColor(colour_for(diagnostic.severity));
        mark.cursor = QTextCursor(block);
        mark.cursor.setPosition(block.position() + diagnostic.primary.col);
        const int length = std::max<int>(diagnostic.primary.len, 1);
        mark.cursor.setPosition(
            std::min(block.position() + diagnostic.primary.col + length,
                     block.position() + block.length() - 1),
            QTextCursor::KeepAnchor);
        selections.append(mark);
    }

    setExtraSelections(selections);
}

void EditorWidget::set_diagnostics(const as::DiagBag& diagnostics) {
    diagnostics_.clear();
    for (const as::Diagnostic& diagnostic : diagnostics.items()) {
        if (diagnostic.primary.line == 0) continue;
        const int line = static_cast<int>(diagnostic.primary.line);
        // Keep the worst one per line: an error outranks a warning outranks a
        // note, so the gutter letter says how bad the line is.
        const auto existing = diagnostics_.find(line);
        if (existing == diagnostics_.end() ||
            static_cast<int>(diagnostic.severity) < static_cast<int>(existing->second.severity)) {
            diagnostics_[line] = diagnostic;
        }
    }
    apply_diagnostic_marks();
}

void EditorWidget::clear_diagnostics() {
    diagnostics_.clear();
    apply_diagnostic_marks();
}

void EditorWidget::apply_diagnostic_marks() {
    highlight_current_line();
    gutter_->update();
}

const as::Diagnostic* EditorWidget::diagnostic_on_line(int line) const {
    const auto found = diagnostics_.find(line);
    return found == diagnostics_.end() ? nullptr : &found->second;
}

void EditorWidget::set_overflow_lines(std::vector<int> lines, QString reason) {
    overflow_ = std::move(lines);
    overflow_reason_ = std::move(reason);
    apply_diagnostic_marks();
    gutter_->update();
}

void EditorWidget::set_execution_line(int line) {
    if (execution_line_ == line) return;
    execution_line_ = line;
    gutter_->update();
}

void EditorWidget::set_breakpoint_lines(std::vector<int> lines) {
    breakpoints_ = std::move(lines);
    gutter_->update();
}

void EditorWidget::set_pipeline_lines(std::vector<PipelineMark> marks) {
    if (marks == pipeline_) return;
    pipeline_ = std::move(marks);
    highlight_current_line();
    gutter_->update();
}

const PipelineMark* EditorWidget::pipeline_mark(int line) const {
    for (const PipelineMark& mark : pipeline_) {
        if (mark.line == line) return &mark;
    }
    return nullptr;
}

int EditorWidget::current_line() const { return textCursor().blockNumber() + 1; }

void EditorWidget::go_to_line(int line) {
    QTextBlock block = document()->findBlockByNumber(line - 1);
    if (!block.isValid()) return;
    QTextCursor cursor(block);
    setTextCursor(cursor);
    centerCursor();
}

}  // namespace rv::gui
