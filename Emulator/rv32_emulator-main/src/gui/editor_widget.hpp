// The assembly editor.
//
// A QPlainTextEdit with a gutter carrying the same three marks the terminal
// version has: a breakpoint dot, the execution arrow, and a severity letter for
// the last assemble's diagnostics. Keeping them identical matters -- the two
// front-ends should feel like one tool seen from different sides.
#pragma once

#include <QPlainTextEdit>
#include <QStringList>
#include <QWidget>

#include <map>
#include <vector>

#include "asm/complete.hpp"
#include "asm/diagnostic.hpp"
#include "asm/symtab.hpp"

namespace rv::gui {

class EditorWidget;

/// Which hue a line is tinted in. Yellow is the ordinary case; a branch that
/// has reached EX has an answer, and says which way it went.
enum class PipelineTone : unsigned char { Pending, Taken, NotTaken };

/// A source line with an instruction in flight, how far down the pipe that
/// instruction has got -- 0 is the stage it was just fetched into, 4 the one
/// where it writes back -- and what it turned out to do. The editor turns that
/// into a name in the margin, and a background on the two depths worth one; it
/// does not need to know what the stages are called.
struct PipelineMark {
    int line = 0;
    int depth = 0;
    PipelineTone tone = PipelineTone::Pending;

    bool operator==(const PipelineMark& other) const {
        return line == other.line && depth == other.depth && tone == other.tone;
    }
};

/// The small block under the caret listing the instructions that match what is
/// being typed. It is only up while a mnemonic is being written: once the
/// instruction is settled the operands take over as ghost text and the block
/// folds away. It floats over the text rather than claiming a row of its own,
/// so opening it never moves the line being written.
class SuggestionBar final : public QWidget {
    Q_OBJECT

public:
    explicit SuggestionBar(QWidget* parent = nullptr);

    /// Open on these candidates, or fold away if there are none.
    void show_for(std::vector<as::Completion> candidates);
    void fold();
    /// Open in the sense of holding candidates -- which is not the same as
    /// isVisible(), since a child of a window that has not been shown yet is
    /// never visible, and the tests run without one.
    bool is_open() const { return !candidates_.empty(); }

    /// Move the highlight, wrapping at both ends.
    void advance(int delta);
    const as::Completion* highlighted() const;
    QStringList entries() const;

    /// Just wide enough for the candidates -- the block is meant to sit under
    /// a word, not to span the editor.
    int content_width() const;
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    std::vector<as::Completion> candidates_;
    int current_ = 0;
};

/// The margin down the left. A separate widget because QPlainTextEdit reserves
/// the space but does not paint it.
class GutterWidget final : public QWidget {
    Q_OBJECT

public:
    explicit GutterWidget(EditorWidget* editor);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    EditorWidget* editor_;
};

class EditorWidget final : public QPlainTextEdit {
    Q_OBJECT

public:
    explicit EditorWidget(QWidget* parent = nullptr);

    int gutter_width() const;
    void paint_gutter(QPaintEvent* event);
    /// 1-based line under a y coordinate in gutter space, or 0.
    int line_at(int y) const;

    /// Diagnostics from the last assemble, shown in the gutter and underlined.
    void set_diagnostics(const as::DiagBag& diagnostics);
    void clear_diagnostics();
    const as::Diagnostic* diagnostic_on_line(int line) const;

    /// Lines whose instructions or data land past the end of the memory they
    /// belong in. Marked rather than removed: the program is what was written,
    /// and cutting it silently would hide the thing worth seeing.
    void set_overflow_lines(std::vector<int> lines, QString reason);
    const std::vector<int>& overflow_lines() const { return overflow_; }

    void set_execution_line(int line);
    /// The line the arrow in the margin is on. 0 when there is none.
    int execution_line() const { return execution_line_; }
    void set_breakpoint_lines(std::vector<int> lines);

    /// Mark the lines whose instructions are in the pipeline: the stage named
    /// in the margin, and a background only on the line writing back or on a
    /// branch that has resolved. An empty list turns the marking off, which is
    /// how the menu toggle works.
    void set_pipeline_lines(std::vector<PipelineMark> marks);
    const std::vector<PipelineMark>& pipeline_lines() const { return pipeline_; }
    /// What is in flight on `line`, or null. Used by the gutter, and by the
    /// tests to ask what a line is showing.
    const PipelineMark* pipeline_mark(int line) const;

    int current_line() const;
    void go_to_line(int line);

    /// Symbols the completer offers as branch and load targets. Refreshed
    /// after every assemble.
    void set_symbols(const as::SymbolTable* symbols) { symbols_ = symbols; }

    /// Offer the instructions matching what is being typed. Bound to
    /// Ctrl+Space, and triggered automatically at two characters.
    void request_completion();
    /// Replace the partial mnemonic with `text`. `settle` closes the bar and
    /// leaves the caret at the operands; without it the bar stays up so Tab
    /// can keep cycling.
    void insert_completion(const QString& text, bool settle = true);
    /// Walk along the bar, putting each candidate in the buffer as it goes.
    void cycle_suggestion(int delta);
    void close_bar();
    SuggestionBar* suggestion_bar() const { return bar_; }
    /// The operands greyed in after the caret. Empty when there is nothing to
    /// fill in. Exposed for the tests, which check the text and not the pixels.
    QString ghost_text() const { return ghost_; }
    /// What the completer is currently offering, for the tests.
    QStringList visible_completions() const;

signals:
    void breakpointToggled(int line);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private slots:
    void update_ghost();
    void update_gutter_width();
    void update_gutter(const QRect& rect, int dy);
    void highlight_current_line();

private:
    void apply_diagnostic_marks();
    /// Put the block under the caret, kept inside the viewport.
    void place_bar();

    GutterWidget* gutter_;
    SuggestionBar* bar_ = nullptr;
    QString ghost_;
    /// Set while the widget is editing the buffer itself, so its own writes do
    /// not count as the user typing and re-open the bar underneath them.
    bool inserting_ = false;
    /// Whether Tab has already put a candidate in the buffer, which decides
    /// whether the next Tab takes the first one or the one after it.
    bool previewing_ = false;
    /// The word the bar opened on, so cycling past the end can put it back.
    QString typed_prefix_;
    const as::SymbolTable* symbols_ = nullptr;
    /// Line number -> the worst diagnostic reported on it.
    std::map<int, as::Diagnostic> diagnostics_;
    std::vector<int> breakpoints_;
    std::vector<int> overflow_;
    std::vector<PipelineMark> pipeline_;
    QString overflow_reason_;
    int execution_line_ = 0;
};

}  // namespace rv::gui
