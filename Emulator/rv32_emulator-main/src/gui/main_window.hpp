// The application window.
//
// Everything below this layer is the same code the terminal front-end and the
// CLI use: DebugSession for the machine, rv_cmd for the command set, the
// assembler's lexer for highlighting. This file is a shell, not a second
// implementation -- which is why the two interfaces cannot disagree about what
// stepping means.
#pragma once

#include <QMainWindow>
#include <QString>
#include <memory>

#include "asm/diagnostic.hpp"
#include "dbg/pipeline.hpp"
#include "dbg/session.hpp"

class QAction;
class QLabel;
class QListWidget;
class QPlainTextEdit;
class QComboBox;
class QTableWidget;
class QSplitter;
class QTabWidget;
class QTimer;

namespace rv::gui {

class EditorWidget;
class EncodingWidget;
class PipelineWidget;
class DevicePanel;

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    /// Build the machine described in a file. Returns false and says why if it
    /// cannot be read.
    bool load_devices(const QString& path);

    /// Write the assembled program as $readmemh images, each padded to the
    /// depth of the memory it belongs to. Either path may be empty to skip it.
    /// Separate from the dialog that asks for the paths, so the writing can be
    /// checked without one.
    /// Select a row of the peripheral list, so a screenshot can show its
    /// controls.
    void select_device(int row);

    bool write_mem_files(const QString& imem_path, const QString& dmem_path, bool annotate);

    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    /// Load a file into the editor and assemble it. Returns false if it did not
    /// assemble; the diagnostics are shown either way.
    bool open_file(const QString& path);

    // Exposed so the offscreen tests can drive the window the way a person
    // would, rather than reaching into its internals.
    void assemble();
    /// One pipeline cycle forward, and one back. The only two there are: a
    /// cycle is the machine's real granularity now, and a source line or a
    /// call is a number of them rather than a separate kind of stepping.
    void step();
    void step_back();
    void run();
    void reset_machine();
    void toggle_breakpoint(int line);

    /// Tint the source lines whose instructions are in the pipeline. On by
    /// default; the View menu turns it off without touching the panel.
    void set_pipeline_colours(bool on);
    bool pipeline_colours() const { return pipeline_colours_; }

    /// Colour a branch green or red once it has resolved. Independent of the
    /// stage marking above, and off leaves a branch looking like anything else.
    void set_branch_colours(bool on);
    bool branch_colours() const { return branch_colours_; }

    dbg::DebugSession& session() { return *session_; }
    EditorWidget& editor() { return *editor_; }
    QString status_text() const;
    /// The last thing said in the message line, for the tests.
    QString message() const { return message_text_; }
    /// Select a dock tab by name, for screenshots and for tests.
    bool show_tab(const QString& title);

private slots:
    void on_new_file();
    void on_open_file();
    void on_save();
    void on_save_as();
    void on_diagnostic_activated(int row);
    void on_cursor_moved();
    void on_memory_edited(int row, int column);
    void on_load_devices();
    void on_export_mem();
    void on_save_devices();
    void on_run_slice();

private:
    void build_actions();
    void build_layout();
    void refresh_all();
    void refresh_registers();
    void refresh_memory();
    /// Mark the lines that do not fit in the machine as it is configured.
    /// Called after assembling and after either memory is resized, because
    /// both ends of that comparison can move.
    void refresh_overflow();
    void on_memory_scrolled();
    void on_memory_resized();
    void sync_memory_sizes();
    void on_memory_export();
    void on_memory_load();

    void refresh_console();

    void refresh_symbols();
    void refresh_diagnostics();
    void refresh_status();
    void refresh_pipeline_marks();
    int execution_line() const;
    void report_stop(const dbg::StopEvent& event);
    void set_message(const QString& text, bool is_error = false);
    bool confirm_discard();

    std::unique_ptr<dbg::DebugSession> session_;
    /// The five-stage picture of that session. A view of the machine, not a
    /// second machine: it commits through the session like everything else.
    dbg::Pipeline pipeline_model_;
    /// Diagnostics from the last assemble *attempt*, successful or not. A
    /// failed attempt never reaches the session -- that is deliberate, so a
    /// broken edit cannot destroy the loaded program -- so its errors have to
    /// be kept here or they would never be shown.
    as::DiagBag last_diagnostics_;

    EditorWidget* editor_ = nullptr;
    EncodingWidget* encoding_ = nullptr;
    PipelineWidget* pipeline_ = nullptr;
    QTableWidget* registers_ = nullptr;
    QTableWidget* memory_ = nullptr;
    QComboBox* imem_size_ = nullptr;
    QComboBox* dmem_size_ = nullptr;
    DevicePanel* devices_ = nullptr;
    QPlainTextEdit* console_ = nullptr;

    QTableWidget* symbols_ = nullptr;
    QListWidget* diagnostics_ = nullptr;
    QTabWidget* dock_ = nullptr;
    QSplitter* whole_ = nullptr;
    QLabel* status_ = nullptr;
    QLabel* message_ = nullptr;

    QAction* action_back_ = nullptr;
    QAction* action_pipeline_colours_ = nullptr;
    QAction* action_branch_colours_ = nullptr;
    bool pipeline_colours_ = true;
    bool branch_colours_ = true;

    /// Drives a long `run` in slices so the window stays responsive without a
    /// worker thread -- speed is not a goal here, and a thread would buy a
    /// class of nondeterministic bugs for nothing.
    QTimer* run_timer_ = nullptr;

    QString path_;
    QString devices_path_;
    /// First address the memory table shows.
    /// The first row drawn last time, so scrolling does not redraw what is
    /// already right.
    int memory_rows_filled_ = -1;
    static constexpr int kMemoryRowsDrawn = 48;
    /// Buffer revision at the last successful assemble, so the status bar can
    /// say whether what is on screen has been built.
    int assembled_revision_ = -1;
    bool message_is_error_ = false;
    QString message_text_;
};

}  // namespace rv::gui
