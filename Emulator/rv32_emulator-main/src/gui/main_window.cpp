#include "gui/main_window.hpp"

#include "core/device_config.hpp"
#include "core/memfile.hpp"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextStream>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>

#include "asm/assembler.hpp"
#include "gui/editor_widget.hpp"
#include "gui/device_panel.hpp"
#include "gui/encoding_widget.hpp"
#include "gui/pipeline_widget.hpp"
#include "isa/disasm.hpp"
#include "isa/regnames.hpp"

namespace rv::gui {
namespace {

/// Instructions per slice while running. Bounds worst-case UI latency while
/// keeping everything on one thread -- speed is not a goal here, and a worker
/// thread would buy a class of nondeterministic bugs for nothing.
constexpr u64 kRunChunk = 20000;

const QColor kChangedColour(0xe8, 0xc0, 0x4e);
const QColor kZeroColour(0x6a, 0x71, 0x7c);
const QColor kNormalColour(0xd6, 0xd6, 0xd6);

QFont mono(int size = 10) {
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPointSize(size);
    return font;
}

QString from(std::string_view text) {
    return QString::fromUtf8(text.data(), static_cast<int>(text.size()));
}

}  // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      session_(std::make_unique<dbg::DebugSession>()),
      pipeline_model_(*session_) {
    setWindowTitle("rv32");
    resize(1280, 820);

    build_layout();
    build_actions();

    run_timer_ = new QTimer(this);
    run_timer_->setInterval(0);
    connect(run_timer_, &QTimer::timeout, this, &MainWindow::on_run_slice);

    refresh_all();
}

MainWindow::~MainWindow() = default;

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

void MainWindow::build_layout() {
    editor_ = new EditorWidget(this);
    connect(editor_, &EditorWidget::breakpointToggled, this, &MainWindow::toggle_breakpoint);
    connect(editor_, &QPlainTextEdit::cursorPositionChanged, this, &MainWindow::on_cursor_moved);

    // Registers: both spellings, because the encoding holds the number and real
    // assembly is written with the name.
    registers_ = new QTableWidget(kNumRegs, 3, this);
    registers_->setHorizontalHeaderLabels({"reg", "hex", "signed"});
    registers_->verticalHeader()->setVisible(false);
    registers_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    registers_->setSelectionMode(QAbstractItemView::NoSelection);
    registers_->setFont(mono());
    registers_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    // Compact rows: 32 registers at the default height need a lot of scrolling,
    // and the point of the panel is to take them in at a glance.
    registers_->verticalHeader()->setDefaultSectionSize(
        QFontMetrics(mono()).height() + 4);
    registers_->setShowGrid(false);

    encoding_ = new EncodingWidget(*session_, &pipeline_model_, this);
    pipeline_ = new PipelineWidget(pipeline_model_, *session_, this);

    const auto make_text = [this] {
        auto* view = new QPlainTextEdit(this);
        view->setReadOnly(true);
        view->setFont(mono());
        view->setLineWrapMode(QPlainTextEdit::NoWrap);
        return view;
    };
    console_ = make_text();

    // An editable table rather than a text dump: being able to type a value
    // into memory while stopped is most of what makes poking at a program
    // useful, and a read-only hex block cannot offer it.
    memory_ = new QTableWidget(32, 5, this);
    memory_->setHorizontalHeaderLabels({"address", "+0", "+4", "+8", "+c"});
    memory_->verticalHeader()->setVisible(false);
    memory_->setFont(mono());
    memory_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    memory_->verticalHeader()->setDefaultSectionSize(QFontMetrics(mono()).height() + 4);
    connect(memory_, &QTableWidget::cellChanged, this, &MainWindow::on_memory_edited);
    connect(memory_->verticalScrollBar(), &QScrollBar::valueChanged, this,
            &MainWindow::on_memory_scrolled);

    // The memory tab is where memory is looked at, so it is also where memory
    // images go in and out. A block was not even viewable before this: the
    // table showed data memory's addresses, and a block lives in the
    // peripheral window.
    auto* memory_page = new QWidget(this);
    auto* memory_layout = new QVBoxLayout(memory_page);
    memory_layout->setContentsMargins(4, 4, 4, 4);
    auto* memory_bar = new QHBoxLayout;

    // The memory sizes live here rather than with the peripherals: they are
    // what this table is a view of, and changing one while looking at the
    // other is the shortest way to see what the number means.
    const auto memory_box = [this, memory_page](const char* label) {
        auto* box = new QComboBox(memory_page);
        for (const u32 size : core::kCommonMemorySizes) {
            box->addItem(QString("%1  %2").arg(label,
                                               QString::fromStdString(
                                                   core::memory_size_label(size))),
                         QVariant(static_cast<uint>(size)));
        }
        connect(box, &QComboBox::currentIndexChanged, this, &MainWindow::on_memory_resized);
        return box;
    };
    imem_size_ = memory_box("imem");
    dmem_size_ = memory_box("dmem");
    memory_bar->addWidget(imem_size_);
    memory_bar->addWidget(dmem_size_);
    memory_bar->addStretch();

    auto* memory_load = new QPushButton("Load .mem…", memory_page);
    auto* memory_export = new QPushButton("Export .mem…", memory_page);
    connect(memory_load, &QPushButton::clicked, this, &MainWindow::on_memory_load);
    connect(memory_export, &QPushButton::clicked, this, &MainWindow::on_memory_export);
    memory_bar->addWidget(memory_load);
    memory_bar->addWidget(memory_export);
    memory_layout->addLayout(memory_bar);
    memory_layout->addWidget(memory_, 1);

    devices_ = new DevicePanel(*session_, this);
    connect(devices_, &DevicePanel::devicesChanged, this, &MainWindow::refresh_all);
    connect(devices_, &DevicePanel::inputChanged, this, &MainWindow::refresh_all);
    // Resizing a memory throws away what no longer fits, so the program that
    // was loaded is no longer necessarily the program in imem. Say so rather
    // than let the next step run off the end of a truncated one.

    symbols_ = new QTableWidget(0, 4, this);
    symbols_->setHorizontalHeaderLabels({"name", "space", "value", "line"});
    symbols_->verticalHeader()->setVisible(false);
    symbols_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    symbols_->setFont(mono());
    symbols_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

    dock_ = new QTabWidget(this);
    dock_->addTab(encoding_, "Encoding");
    dock_->addTab(pipeline_, "Pipeline");
    dock_->addTab(memory_page, "Memory");
    dock_->addTab(console_, "Console");
    dock_->addTab(devices_, "Devices");
    dock_->addTab(symbols_, "Symbols");

    diagnostics_ = new QListWidget(this);
    diagnostics_->setFont(mono());
    // The hints are the useful half of a diagnostic and they are often long;
    // truncating them would throw away the part that tells you what to do.
    diagnostics_->setWordWrap(true);
    connect(diagnostics_, &QListWidget::currentRowChanged, this,
            &MainWindow::on_diagnostic_activated);

    auto* right = new QSplitter(Qt::Vertical, this);
    right->addWidget(registers_);
    right->addWidget(dock_);
    right->setSizes({320, 420});

    auto* top = new QSplitter(Qt::Horizontal, this);
    top->addWidget(editor_);
    top->addWidget(right);
    // Explicit sizes rather than stretch factors: the register table's size
    // hint is wide enough to squeeze the editor down to a column of fragments,
    // and reading the code is the whole point.
    top->setSizes({760, 520});

    whole_ = new QSplitter(Qt::Vertical, this);
    whole_->addWidget(top);
    whole_->addWidget(diagnostics_);
    whole_->setSizes({740, 0});  // the list opens only when there is something in it

    setCentralWidget(whole_);

    status_ = new QLabel(this);
    message_ = new QLabel(this);
    status_->setFont(mono());
    statusBar()->addWidget(message_, 1);
    statusBar()->addPermanentWidget(status_);
}

void MainWindow::build_actions() {
    auto* file_menu = menuBar()->addMenu("&File");
    file_menu->addAction("&New", QKeySequence::New, this, &MainWindow::on_new_file);
    file_menu->addAction("&Open…", QKeySequence::Open, this, &MainWindow::on_open_file);
    file_menu->addAction("&Save", QKeySequence::Save, this, &MainWindow::on_save);
    file_menu->addAction("Save &As…", QKeySequence::SaveAs, this, &MainWindow::on_save_as);
    file_menu->addSeparator();
    file_menu->addAction("Load &peripherals…", this, &MainWindow::on_load_devices);
    file_menu->addAction("Save p&eripherals…", this, &MainWindow::on_save_devices);
    file_menu->addSeparator();
    file_menu->addAction("&Generate .mem…", this, &MainWindow::on_export_mem);
    file_menu->addSeparator();
    file_menu->addAction("&Quit", QKeySequence::Quit, this, &QWidget::close);

    auto* toolbar = addToolBar("Run");
    toolbar->setMovable(false);

    // The same keys as the terminal front-end, so switching between them does
    // not mean relearning anything.
    const auto add = [&](const QString& text, const QKeySequence& key, auto slot,
                         const QString& tip) {
        QAction* action = toolbar->addAction(text);
        action->setShortcut(key);
        action->setToolTip(tip + "  (" + key.toString() + ")");
        connect(action, &QAction::triggered, this, slot);
        return action;
    };

    add("Assemble", Qt::Key_F5, &MainWindow::assemble, "Assemble the buffer and load it");
    // Next to Assemble because it is the other half of the same sentence: one
    // turns the text into a machine, the other turns the machine into files a
    // synthesis tool will take.
    add("Generate", QKeySequence("Ctrl+E"), &MainWindow::on_export_mem,
        "Write imem.mem and dmem.mem, padded to this machine's depth");
    toolbar->addSeparator();
    add("Step", Qt::Key_F8, &MainWindow::step,
        "One pipeline cycle — an instruction takes effect when it reaches WB");
    action_back_ = add("Back", Qt::Key_F6, &MainWindow::step_back, "Undo one pipeline cycle");
    add("Run", Qt::Key_F9, &MainWindow::run, "Run until something stops it");
    add("Reset", Qt::Key_F4, &MainWindow::reset_machine, "Restart, keeping breakpoints");

    auto* run_menu = menuBar()->addMenu("&Run");
    for (QAction* action : toolbar->actions()) {
        if (!action->isSeparator()) run_menu->addAction(action);
    }

    // Not on the toolbar: it is a preference about how the editor looks, not
    // something you reach for while stepping, and the toolbar is already as
    // wide as the window can spare.
    auto* view_menu = menuBar()->addMenu("&View");
    action_pipeline_colours_ = view_menu->addAction("Pipeline colours in the editor");
    action_pipeline_colours_->setCheckable(true);
    action_pipeline_colours_->setChecked(pipeline_colours_);
    action_pipeline_colours_->setShortcut(QKeySequence("Ctrl+Shift+P"));
    connect(action_pipeline_colours_, &QAction::toggled, this, &MainWindow::set_pipeline_colours);

    action_branch_colours_ = view_menu->addAction("Green and red on a resolved branch");
    action_branch_colours_->setCheckable(true);
    action_branch_colours_->setChecked(branch_colours_);
    action_branch_colours_->setShortcut(QKeySequence("Ctrl+Shift+B"));
    connect(action_branch_colours_, &QAction::toggled, this, &MainWindow::set_branch_colours);
}

void MainWindow::set_pipeline_colours(bool on) {
    pipeline_colours_ = on;
    if (action_pipeline_colours_ != nullptr && action_pipeline_colours_->isChecked() != on) {
        action_pipeline_colours_->setChecked(on);
    }
    // With nothing marked at all there is no branch to colour either.
    if (action_branch_colours_ != nullptr) action_branch_colours_->setEnabled(on);
    refresh_pipeline_marks();
}

void MainWindow::set_branch_colours(bool on) {
    branch_colours_ = on;
    if (action_branch_colours_ != nullptr && action_branch_colours_->isChecked() != on) {
        action_branch_colours_->setChecked(on);
    }
    refresh_pipeline_marks();
}

namespace {

/// What a slot says about itself in colour. Yellow unless it is a branch or a
/// jump that has already run. Real hardware resolves the condition in EX, but
/// this emulator executes a whole instruction at once, and that happens at
/// write-back -- so write-back is the first cycle there is anything to claim.
PipelineTone tone_of(const dbg::Slot& slot) {
    if (!slot.committed || !slot.instr.valid()) return PipelineTone::Pending;
    const isa::InstrGroup group = slot.instr.desc().group;
    if (group != isa::InstrGroup::Branch && group != isa::InstrGroup::Jump) {
        return PipelineTone::Pending;
    }
    return slot.redirected ? PipelineTone::Taken : PipelineTone::NotTaken;
}

}  // namespace

// Where the arrow in the margin points: at the line that just wrote back, so
// that it, the yellow mark and the encoding panel all agree. Before anything
// has run there is nothing in write-back, and the pc is the honest answer.
int MainWindow::execution_line() const {
    const dbg::Slot& retired = pipeline_model_.at(dbg::Stage::WB);
    if (retired.valid) {
        if (const std::optional<u32> line = session_->map().line_of(retired.pc)) {
            return static_cast<int>(*line);
        }
    }
    return static_cast<int>(session_->current_line().value_or(0));
}

// Which source lines have an instruction in flight, and how far along it is.
//
// Strictly by stage: a line shows its *most advanced* instruction. The stages
// decide, and the line is only where they happen to be drawn -- so when a
// two-word pseudo-instruction straddles ID and EX, the line is at EX, because
// that is the furthest the pipe has got on it -- see below, where
// that is where the pipeline has got to on it.
void MainWindow::refresh_pipeline_marks() {
    if (!pipeline_colours_) {
        editor_->set_pipeline_lines({});
        return;
    }

    std::map<int, PipelineMark> deepest;
    for (std::size_t depth = 0; depth < dbg::kNumStages; ++depth) {
        const dbg::Slot& slot = pipeline_model_.at(static_cast<dbg::Stage>(depth));
        if (!slot.valid) continue;
        const std::optional<u32> line = session_->map().line_of(slot.pc);
        if (!line.has_value()) continue;

        const int number = static_cast<int>(*line);
        const PipelineMark mark{number, static_cast<int>(depth),
                                branch_colours_ ? tone_of(slot) : PipelineTone::Pending};
        const auto found = deepest.find(number);
        if (found == deepest.end() || mark.depth > found->second.depth) deepest[number] = mark;
    }

    std::vector<PipelineMark> marks;
    marks.reserve(deepest.size());
    for (const auto& [line, mark] : deepest) marks.push_back(mark);
    editor_->set_pipeline_lines(std::move(marks));
}

// ---------------------------------------------------------------------------
// Files
// ---------------------------------------------------------------------------

bool MainWindow::open_file(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        set_message("cannot open " + path, true);
        return false;
    }
    QTextStream stream(&file);
    editor_->setPlainText(stream.readAll());
    editor_->document()->setModified(false);
    path_ = path;
    setWindowTitle("rv32 — " + QFileInfo(path).fileName());
    return assemble(), session_->has_program();
}

void MainWindow::on_new_file() {
    if (!confirm_discard()) return;
    editor_->setPlainText("        .text\n        .globl _start\n_start:\n        li a0, 1\n"
                          "        ebreak\n");
    editor_->document()->setModified(false);
    path_.clear();
    setWindowTitle("rv32 — untitled");
    assemble();
}

void MainWindow::on_open_file() {
    if (!confirm_discard()) return;
    const QString path =
        QFileDialog::getOpenFileName(this, "Open assembly", {}, "Assembly (*.s *.S *.asm)");
    if (!path.isEmpty()) open_file(path);
}

void MainWindow::on_save() {
    if (path_.isEmpty()) {
        on_save_as();
        return;
    }
    QFile file(path_);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        set_message("cannot write " + path_, true);
        return;
    }
    QTextStream(&file) << editor_->toPlainText();
    editor_->document()->setModified(false);
    set_message("saved " + path_);
}

void MainWindow::on_save_as() {
    const QString path =
        QFileDialog::getSaveFileName(this, "Save assembly", path_, "Assembly (*.s)");
    if (path.isEmpty()) return;
    path_ = path;
    setWindowTitle("rv32 — " + QFileInfo(path).fileName());
    on_save();
}

bool MainWindow::confirm_discard() {
    if (!editor_->document()->isModified()) return true;
    const auto answer =
        QMessageBox::question(this, "rv32", "The buffer has unsaved changes. Discard them?",
                              QMessageBox::Discard | QMessageBox::Cancel);
    return answer == QMessageBox::Discard;
}

// ---------------------------------------------------------------------------
// Execution
// ---------------------------------------------------------------------------

void MainWindow::assemble() {
    run_timer_->stop();

    const QByteArray text = editor_->toPlainText().toUtf8();
    as::AssembleOptions options;
    options.explain_pseudo_sizing = false;  // the diagnostics list has no room for prose
    options.imem_size = session_->hart().imem().size();
    options.dmem_size = session_->hart().bus().dmem().size();
    options.imem_base = session_->hart().imem().base();
    options.dmem_base = session_->hart().bus().dmem().base();
    options.reset_entry = session_->hart().reset_entry();

    const std::string name =
        path_.isEmpty() ? std::string("<editor>") : path_.toStdString();

    // Assemble a detached copy first, so a broken edit never destroys the
    // program that is currently loaded and steppable.
    const as::SourceFile candidate(name, text.toStdString());
    as::AssembledProgram attempt = as::assemble(candidate, options);

    editor_->set_diagnostics(attempt.diagnostics);
    last_diagnostics_ = attempt.diagnostics;
    assembled_revision_ = editor_->document()->revision();

    if (!attempt.ok()) {
        refresh_diagnostics();
        if (const as::Diagnostic* first = attempt.diagnostics.first_error()) {
            editor_->go_to_line(static_cast<int>(first->primary.line));
            set_message(from(first->message), true);
        }
        refresh_status();
        return;
    }

    session_->load_source(text.toStdString(), name, options);
    // The completer offers this program's labels as branch and load targets.
    editor_->set_symbols(&session_->symbols());
    pipeline_model_.reset();
    encoding_->follow_pc();
    set_message(QString::asprintf("assembled: %zu instructions, %zu bytes of data",
                                  session_->program().map.entries().size(),
                                  session_->program().dmem_bytes.size()));
    refresh_all();
}

void MainWindow::report_stop(const dbg::StopEvent& event) {
    switch (event.reason) {
        case dbg::StopReason::Step: message_text_.clear(); break;
        case dbg::StopReason::Breakpoint:
            set_message(QString::asprintf("breakpoint %u at line %u", event.breakpoint_id,
                                          event.line.value_or(0)));
            break;
        case dbg::StopReason::Watchpoint:
            set_message(QString::asprintf("watchpoint at 0x%08x: 0x%08x → 0x%08x",
                                          event.watch_addr, event.watch_old, event.watch_new));
            break;
        case dbg::StopReason::Halted:
            set_message(QString("stopped: ") + core::halt_reason_name(event.halt));
            break;
        case dbg::StopReason::Trap:
            set_message(QString("trap: ") + core::trap_cause_name(event.cause), true);
            break;
        case dbg::StopReason::Watchdog:
            set_message("stopped after the instruction budget — infinite loop?", true);
            break;
        case dbg::StopReason::NotRunning: set_message("no program loaded", true); break;
        case dbg::StopReason::HistoryHorizon:
            set_message("no further history: this is as far back as the recording goes", true);
            break;
    }
    if (event.reason != dbg::StopReason::Step) run_timer_->stop();
    refresh_all();
}

// One step, and it is a cycle. A source line, a call, a whole program are all
// some number of these -- there is nothing a separate control would express
// that holding this one down does not, and every extra button was a second way
// to ask the same question.
void MainWindow::step() { report_stop(pipeline_model_.tick()); }

void MainWindow::step_back() {
    if (!pipeline_model_.reverse_tick()) {
        set_message("no further history: this pipeline has not run a cycle yet", true);
        return;
    }
    // Going back is never itself a reason to stop, so there is no event to
    // report -- a plain Step is what clears the message and repaints.
    report_stop(dbg::StopEvent{});
}

void MainWindow::run() {
    if (run_timer_->isActive()) {
        run_timer_->stop();
        set_message("interrupted");
        refresh_all();
        return;
    }
    if (!session_->has_program()) {
        set_message("no program loaded", true);
        return;
    }
    message_text_.clear();
    run_timer_->start();
}

void MainWindow::on_run_slice() {
    if (const auto event = session_->run_chunk(kRunChunk)) {
        pipeline_model_.resync();
        report_stop(*event);
        return;
    }
    // Still going: keep the panels current so a long run is watchable.
    pipeline_model_.resync();
    refresh_registers();
    refresh_status();
    pipeline_->update();
}

void MainWindow::reset_machine() {
    run_timer_->stop();
    session_->reset();
    pipeline_model_.reset();
    encoding_->follow_pc();
    set_message("machine reset; breakpoints kept");
    refresh_all();
}

void MainWindow::toggle_breakpoint(int line) {
    if (!session_->has_program()) return;
    const bool added =
        session_->breakpoints().toggle_line(static_cast<u32>(line), session_->map());
    set_message(added ? QString::asprintf("breakpoint set on line %d", line)
                      : QString::asprintf("breakpoint cleared on line %d", line));
    refresh_all();
}

// ---------------------------------------------------------------------------
// Refreshing
// ---------------------------------------------------------------------------

void MainWindow::refresh_all() {
    refresh_registers();
    refresh_memory();
    refresh_overflow();
    refresh_console();
    refresh_symbols();
    if (devices_ != nullptr) devices_->refresh();
    refresh_diagnostics();
    refresh_status();

    std::vector<int> breakpoints;
    for (const dbg::Breakpoint& breakpoint : session_->breakpoints().breakpoints()) {
        if (breakpoint.enabled) breakpoints.push_back(static_cast<int>(breakpoint.line));
    }
    editor_->set_breakpoint_lines(std::move(breakpoints));
    editor_->set_execution_line(execution_line());

    action_back_->setEnabled(pipeline_model_.can_reverse());
    refresh_pipeline_marks();
    encoding_->update();
    pipeline_->update();
}

void MainWindow::refresh_registers() {
    const auto changed = [this](RegIdx reg) {
        for (const dbg::RegChange& change : session_->last_changes()) {
            if (change.index == reg) return true;
        }
        return false;
    };

    for (RegIdx reg = 0; reg < kNumRegs; ++reg) {
        const u32 value = session_->cpu().x[reg];
        const bool just_changed = changed(reg);

        const auto set = [&](int column, const QString& text, const QColor& colour) {
            QTableWidgetItem* item = registers_->item(reg, column);
            if (item == nullptr) {
                item = new QTableWidgetItem;
                registers_->setItem(reg, column, item);
            }
            item->setText(text);
            item->setForeground(colour);
            QFont font = mono();
            font.setBold(just_changed);
            item->setFont(font);
        };

        // Dimming the zeros is what makes the two or three registers a small
        // program actually uses stand out of a wall of 32.
        const QColor colour = just_changed ? kChangedColour
                                           : (value == 0 ? kZeroColour : kNormalColour);
        set(0, from(isa::numeric_name(reg)) + "  " + from(isa::abi_name(reg)), colour);
        set(1, QString::asprintf("%08x", value), colour);
        set(2, QString::number(static_cast<i32>(value)), colour);
    }

    // Scroll the register that just changed into view. Without this the whole
    // highlight is wasted whenever the write lands on one of the high
    // registers, which is exactly when you most want to see it.
    for (const dbg::RegChange& change : session_->last_changes()) {
        if (QTableWidgetItem* item = registers_->item(change.index, 0)) {
            registers_->scrollToItem(item, QAbstractItemView::EnsureVisible);
        }
    }
}

void MainWindow::refresh_overflow() {
    if (!session_->has_program()) {
        editor_->set_overflow_lines({}, {});
        return;
    }

    const as::AssembledProgram& program = session_->program();
    const u32 imem_bytes = session_->hart().imem().size();
    const u32 dmem_bytes = session_->hart().bus().dmem().size();

    // The instructions that landed past the end of instruction memory. Found
    // through the source map rather than by counting lines, because one line
    // can be two instructions and the second may be the one that does not fit.
    std::vector<int> lines;
    for (const as::AddrEntry& entry : program.map.entries()) {
        if (entry.addr >= imem_bytes) lines.push_back(static_cast<int>(entry.line));
    }
    const bool code_overflows = !lines.empty();
    const bool data_overflows = program.dmem_bytes.size() > dmem_bytes;

    std::sort(lines.begin(), lines.end());
    lines.erase(std::unique(lines.begin(), lines.end()), lines.end());

    QString reason;
    if (code_overflows) {
        // Bytes rather than instructions: with the C extension "how many
        // instructions too many" depends on how many of them are compressed,
        // so it is not a number that can be stated.
        reason = QString("%1 bytes of code past the end of imem (%2 KB)")
                     .arg(program.imem_bytes - imem_bytes)
                     .arg(imem_bytes / 1024);
    }
    if (data_overflows) {
        if (!reason.isEmpty()) reason += "; ";
        reason += QString("%1 bytes of data past the end of dmem (%2 KB)")
                      .arg(program.dmem_bytes.size() - dmem_bytes)
                      .arg(dmem_bytes / 1024);
    }
    editor_->set_overflow_lines(std::move(lines), reason);

    // Data has no lines to mark -- .data sits in its own space and the map
    // covers instructions -- so it says so in the message bar instead of
    // nowhere.
    if (data_overflows && !code_overflows) set_message(reason, true);
}

void MainWindow::sync_memory_sizes() {
    const auto select = [](QComboBox* box, u32 size, const char* label) {
        const QSignalBlocker blocker(box);
        const int found = box->findData(QVariant(static_cast<uint>(size)));
        if (found >= 0) {
            box->setCurrentIndex(found);
            return;
        }
        // A board file may name a size the list does not offer. Showing it is
        // the only honest thing to do; the alternative is a box that says one
        // size while the machine has another.
        box->addItem(
            QString("%1  %2").arg(label, QString::fromStdString(core::memory_size_label(size))),
            QVariant(static_cast<uint>(size)));
        box->setCurrentIndex(box->count() - 1);
    };
    select(imem_size_, session_->hart().imem().size(), "imem");
    select(dmem_size_, session_->hart().bus().dmem().size(), "dmem");
}

void MainWindow::on_memory_resized() {
    session_->hart().resize_memories(imem_size_->currentData().toUInt(),
                                     dmem_size_->currentData().toUInt());
    sync_memory_sizes();
    memory_rows_filled_ = -1;
    // Resizing throws away what no longer fits, so the program that was loaded
    // is no longer necessarily the program in imem.
    set_message("memory resized — press F5 to assemble into the new machine");
    refresh_all();
}

void MainWindow::on_memory_export() {
    const QString path = QFileDialog::getSaveFileName(this, "Export data memory",
                                                      QDir::currentPath(), "Memory image (*.mem)");
    if (path.isEmpty()) return;

    const u32 size = session_->hart().bus().dmem().size();
    std::vector<Word> words;
    words.reserve(size / 4);
    for (u32 offset = 0; offset < size; offset += 4) {
        words.push_back(session_->read_dmem_word(offset));
    }

    core::MemFileOptions options;
    // Padded to the memory, which is the point: the file is this memory, so it
    // is as deep as this memory whatever is written in it.
    options.pad_to_full = true;
    options.full_size = size / 4;

    const core::MemFileResult result = core::write_mem_file(path.toStdString(), words, options);
    if (!result.ok) {
        QMessageBox::warning(this, "rv32", QString::fromStdString(result.error));
        return;
    }
    set_message(
        QString("wrote %1 — %2 × 32-bit").arg(QFileInfo(path).fileName()).arg(size / 4));
}

void MainWindow::on_memory_load() {
    const QString path = QFileDialog::getOpenFileName(this, "Load data memory",
                                                      QDir::currentPath(), "Memory image (*.mem)");
    if (path.isEmpty()) return;

    std::vector<Word> words;
    const core::MemFileResult result = core::read_mem_file(path.toStdString(), words);
    if (!result.ok) {
        QMessageBox::warning(this, "rv32", QString::fromStdString(result.error));
        return;
    }

    const u32 size = session_->hart().bus().dmem().size();
    const bool truncated = words.size() * 4 > size;
    if (truncated) words.resize(size / 4);
    for (std::size_t index = 0; index < words.size(); ++index) {
        session_->hart().bus().poke_word(static_cast<Addr>(index * 4), words[index]);
    }

    memory_rows_filled_ = -1;
    refresh_all();
    set_message(truncated ? QString("%1 is longer than data memory; the rest was not read")
                                .arg(QFileInfo(path).fileName())
                          : QString("read %1 words from %2")
                                .arg(words.size())
                                .arg(QFileInfo(path).fileName()),
                truncated);
}

void MainWindow::refresh_memory() {
    // The table is as long as the memory is. This is the whole of what the
    // dmem size means on screen: shrink it and the view stops where the
    // machine does, rather than scrolling on into addresses that read as zero
    // because nothing is there to say otherwise.
    sync_memory_sizes();
    // Always data memory. A block is looked at where it is configured, under
    // the device it belongs to, rather than through a list on a tab that is
    // about something else.
    const Addr base = 0;
    const u32 region_bytes = session_->hart().bus().dmem().size();
    const int rows = static_cast<int>(region_bytes / 16);
    if (memory_->rowCount() != rows) {
        const QSignalBlocker resize_blocker(memory_);
        memory_->setRowCount(rows);
        memory_rows_filled_ = -1;  // the rows moved; whatever was drawn is stale
    }
    if (rows == 0) return;

    const Addr highlight = session_->last_step().mem_written;
    if (highlight != core::kNoAddr && highlight >= base && highlight - base < region_bytes) {
        memory_->scrollToItem(memory_->item(static_cast<int>((highlight - base) / 16), 0),
                              QAbstractItemView::PositionAtCenter);
    }

    // Only the rows on screen are filled. A megabyte of data memory is 65 536
    // of them, and filling all of them after every step would make stepping
    // slower than the program.
    const int first = std::max(0, memory_->rowAt(0));
    const int last = std::min(rows - 1, first + kMemoryRowsDrawn);
    if (first == memory_rows_filled_ && highlight == core::kNoAddr) return;
    memory_rows_filled_ = first;

    // Blocking keeps the programmatic fill from being mistaken for the user
    // typing, which would write memory back to itself on every step.
    const QSignalBlocker blocker(memory_);
    for (int row = first; row <= last; ++row) {
        const Addr address = base + static_cast<Addr>(row) * 16;

        const auto set = [&](int column, const QString& text, bool editable, bool changed) {
            QTableWidgetItem* item = memory_->item(row, column);
            if (item == nullptr) {
                item = new QTableWidgetItem;
                memory_->setItem(row, column, item);
            }
            item->setText(text);
            item->setFlags(editable ? (Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable)
                                    : (Qt::ItemIsEnabled | Qt::ItemIsSelectable));
            item->setForeground(changed ? kChangedColour
                                        : (editable ? kNormalColour : kZeroColour));
        };

        set(0, QString::asprintf("%08x", address), false, false);
        for (int index = 0; index < 4; ++index) {
            const Addr at = address + static_cast<Addr>(index * 4);
            const u32 value = session_->read_dmem_word(at);
            set(index + 1, QString::asprintf("%08x", value), true, at == highlight);
        }
    }
}

void MainWindow::on_memory_scrolled() { refresh_memory(); }

void MainWindow::on_memory_edited(int row, int column) {
    if (column < 1 || column > 4) return;
    QTableWidgetItem* item = memory_->item(row, column);
    if (item == nullptr) return;

    bool valid = false;
    const u32 value = item->text().trimmed().toUInt(&valid, 16);
    if (!valid) {
        set_message("not a hex value: " + item->text(), true);
        refresh_memory();
        return;
    }

    const Addr address = static_cast<Addr>(row) * 16 + static_cast<Addr>((column - 1) * 4);
    // poke rather than store: this is the debugger writing, so it must not
    // trigger whatever a device would do on a write from the program.
    session_->hart().bus().poke_word(address, value);
    set_message(QString::asprintf("[0x%08x] = 0x%08x", address, value));
    refresh_all();
}

void MainWindow::refresh_console() {
    const QString text = QString::fromStdString(session_->uart_output());
    console_->setPlainText(text.isEmpty() ? "(nothing written to the uart yet)" : text);
}

void MainWindow::refresh_symbols() {
    const auto& symbols = session_->symbols().all();
    symbols_->setRowCount(static_cast<int>(symbols.size()));
    for (int row = 0; row < static_cast<int>(symbols.size()); ++row) {
        const as::Symbol& symbol = symbols[static_cast<std::size_t>(row)];
        const QStringList cells = {QString::fromStdString(symbol.name),
                                   as::space_name(symbol.space),
                                   QString::asprintf("0x%08x", symbol.value),
                                   QString::number(symbol.definition.line)};
        for (int column = 0; column < cells.size(); ++column) {
            auto* item = new QTableWidgetItem(cells[column]);
            // Colour by space: this is the distinction that explains why
            // jumping to a data symbol gets rejected.
            item->setForeground(symbol.space == as::Space::Data ? QColor(0xe8, 0xa0, 0x4e)
                                                                : QColor(0x89, 0xd1, 0x85));
            symbols_->setItem(row, column, item);
        }
    }
}

void MainWindow::refresh_diagnostics() {
    diagnostics_->clear();
    // Give the list room only when it has something to say; an empty pane at
    // the bottom of the window is pure waste.
    const bool any = !last_diagnostics_.items().empty();
    if (whole_ != nullptr) whole_->setSizes(any ? QList<int>{600, 160} : QList<int>{740, 0});
    for (const as::Diagnostic& diagnostic : last_diagnostics_.items()) {
        QString text = QString::asprintf("%s:%u:%u  ",
                                         QFileInfo(path_).fileName().toUtf8().constData(),
                                         diagnostic.primary.line, diagnostic.primary.col + 1);
        text += as::severity_name(diagnostic.severity);
        if (!diagnostic.code.empty()) text += "[" + from(diagnostic.code) + "]";
        text += ": " + from(diagnostic.message);
        if (!diagnostic.hint.empty()) text += "   — " + from(diagnostic.hint);

        auto* item = new QListWidgetItem(text);
        item->setData(Qt::UserRole, diagnostic.primary.line);
        item->setForeground(diagnostic.severity == as::Severity::Error ? QColor(0xf2, 0x6d, 0x6d)
                                                                       : QColor(0xe8, 0xc0, 0x4e));
        diagnostics_->addItem(item);
    }
}

QString MainWindow::status_text() const { return status_->text(); }

bool MainWindow::show_tab(const QString& title) {
    for (int index = 0; index < dock_->count(); ++index) {
        if (dock_->tabText(index).compare(title, Qt::CaseInsensitive) == 0) {
            dock_->setCurrentIndex(index);
            return true;
        }
    }
    return false;
}

void MainWindow::refresh_status() {
    const core::CpuState& cpu = session_->cpu();
    QString text = QString::asprintf("pc %08x   cycle %llu   instret %llu",
                                     cpu.pc, static_cast<unsigned long long>(cpu.cycle),
                                     static_cast<unsigned long long>(cpu.instret));

    // Which word of its source line the pc is on. That [1/2] is how a
    // two-instruction `li` stops being a mystery.
    if (const auto slot = session_->current_slot(); slot && slot->second > 1) {
        text += QString::asprintf("   [%u/%u]", slot->first + 1, slot->second);
    }
    if (session_->halted()) text += "   HALTED";
    if (run_timer_->isActive()) text += "   RUNNING";
    if (editor_->document()->revision() != assembled_revision_) text += "   (not assembled)";

    status_->setText(text);
    message_->setText(message_text_);
    message_->setStyleSheet(message_is_error_ ? "color: #f26d6d;" : "");
}

void MainWindow::set_message(const QString& text, bool is_error) {
    message_text_ = text;
    message_is_error_ = is_error;
    if (message_ != nullptr) {
        message_->setText(text);
        message_->setStyleSheet(is_error ? "color: #f26d6d;" : "");
    }
}

// ---------------------------------------------------------------------------
// Reacting
// ---------------------------------------------------------------------------

void MainWindow::on_diagnostic_activated(int row) {
    if (row < 0) return;
    QListWidgetItem* item = diagnostics_->item(row);
    if (item == nullptr) return;
    editor_->go_to_line(item->data(Qt::UserRole).toInt());
    editor_->setFocus();
}

bool MainWindow::load_devices(const QString& path) {
    if (!devices_->load_config(path)) return false;
    devices_path_ = path;
    const QString warning = devices_->last_warning();
    set_message(warning.isEmpty() ? "machine built from " + path
                                  : "machine built from " + path + " — " + warning);
    refresh_all();
    return true;
}

void MainWindow::on_load_devices() {
    const QString path = QFileDialog::getOpenFileName(this, "Load peripherals", devices_path_,
                                                      "Device configuration (*.toml)");
    if (path.isEmpty()) return;
    load_devices(path);
}

void MainWindow::on_save_devices() {
    const QString path = QFileDialog::getSaveFileName(this, "Save peripherals", devices_path_,
                                                      "Device configuration (*.toml)");
    if (path.isEmpty()) return;
    if (devices_->save_config(path)) {
        devices_path_ = path;
        set_message("peripherals saved to " + path);
    }
}

void MainWindow::on_cursor_moved() {
    // The encoding panel follows the caret, so a line explains itself while it
    // is still being written.
    const int line = editor_->current_line();
    const auto& addrs = session_->map().addrs_for_line(static_cast<u32>(line));
    if (!addrs.empty()) {
        encoding_->set_address(addrs.front());
        if (dock_->currentWidget() != encoding_) encoding_->update();
    }

    if (const as::Diagnostic* diagnostic = editor_->diagnostic_on_line(line)) {
        set_message(from(diagnostic->message), diagnostic->severity == as::Severity::Error);
    }
    refresh_status();
}

namespace {

/// Where the .mem files go and what they are called. It shows the depth of
/// each image beside it, because that is the number the Block Memory Generator
/// asks for on the other side -- getting it wrong is the mistake that produces
/// a memory full of the right words in the wrong places.
class ExportMemDialog final : public QDialog {
public:
    ExportMemDialog(QWidget* parent, u32 imem_bytes, u32 dmem_bytes)
        : QDialog(parent) {
        setWindowTitle("Export .mem");
        auto* form = new QFormLayout(this);

        folder_ = new QLineEdit(QDir::currentPath(), this);
        auto* browse = new QPushButton("Browse…", this);
        connect(browse, &QPushButton::clicked, this, [this] {
            const QString picked =
                QFileDialog::getExistingDirectory(this, "Export to", folder_->text());
            if (!picked.isEmpty()) folder_->setText(picked);
        });
        auto* folder_row = new QHBoxLayout;
        folder_row->addWidget(folder_, 1);
        folder_row->addWidget(browse);
        form->addRow("Folder", folder_row);

        base_ = new QLineEdit("rv32", this);
        form->addRow("Base name", base_);

        imem_ = new QCheckBox(QString("imem  —  %1 × 32-bit").arg(imem_bytes / 4), this);
        dmem_ = new QCheckBox(QString("dmem  —  %1 × 32-bit").arg(dmem_bytes / 4), this);
        imem_->setChecked(true);
        dmem_->setChecked(true);
        form->addRow("Write", imem_);
        form->addRow("", dmem_);

        annotate_ = new QCheckBox("comment each imem word with its disassembly", this);
        form->addRow("", annotate_);

        auto* note = new QLabel(
            "Each file is padded to the full depth, so $readmemh fills the whole "
            "array and nothing is left holding whatever the tool put there.",
            this);
        note->setWordWrap(true);
        note->setStyleSheet("color: #7d8590;");
        form->addRow("", note);

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        buttons->button(QDialogButtonBox::Ok)->setText("Export");
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        form->addRow(buttons);
    }

    QString imem_path() const { return path_for("imem"); }
    QString dmem_path() const { return path_for("dmem"); }
    bool wants_imem() const { return imem_->isChecked(); }
    bool wants_dmem() const { return dmem_->isChecked(); }
    bool annotate() const { return annotate_->isChecked(); }

private:
    QString path_for(const char* which) const {
        return QDir(folder_->text()).filePath(base_->text() + "_" + which + ".mem");
    }

    QLineEdit* folder_ = nullptr;
    QLineEdit* base_ = nullptr;
    QCheckBox* imem_ = nullptr;
    QCheckBox* dmem_ = nullptr;
    QCheckBox* annotate_ = nullptr;
};

}  // namespace

void MainWindow::select_device(int row) {
    show_tab("Devices");
    devices_->select_row(row);
}

bool MainWindow::write_mem_files(const QString& imem_path, const QString& dmem_path,
                                 bool annotate) {
    if (!session_->has_program()) {
        set_message("nothing to export — press F5 to assemble first");
        return false;
    }

    const as::AssembledProgram& program = session_->program();
    QStringList written;

    core::MemFileOptions options;
    // Always padded. A .mem shorter than the array it loads into leaves the
    // tail holding whatever the tool put there, and that is the class of bug
    // that looks like the program going wrong rather than the file being short.
    options.pad_to_full = true;

    if (!imem_path.isEmpty()) {
        options.full_size = session_->hart().imem().size() / 4;
        options.annotate = annotate;
        const core::MemFileResult result =
            core::write_mem_file(imem_path.toStdString(), program.imem_words, options);
        if (!result.ok) {
            QMessageBox::warning(this, "rv32", QString::fromStdString(result.error));
            return false;
        }
        written << QFileInfo(imem_path).fileName();
    }

    if (!dmem_path.isEmpty()) {
        options.full_size = session_->hart().bus().dmem().size() / 4;
        options.annotate = false;  // disassembling data would be nonsense
        const std::vector<Word> words = core::pack_bytes_to_words(program.dmem_bytes);
        const core::MemFileResult result =
            core::write_mem_file(dmem_path.toStdString(), words, options);
        if (!result.ok) {
            QMessageBox::warning(this, "rv32", QString::fromStdString(result.error));
            return false;
        }
        written << QFileInfo(dmem_path).fileName();
    }

    if (written.isEmpty()) {
        set_message("nothing selected to export");
        return false;
    }
    set_message("wrote " + written.join(" and ") + " to " +
                QFileInfo(imem_path.isEmpty() ? dmem_path : imem_path).path());
    return true;
}

void MainWindow::on_export_mem() {
    if (!session_->has_program()) {
        set_message("nothing to export — press F5 to assemble first");
        return;
    }

    ExportMemDialog dialog(this, session_->hart().imem().size(),
                           session_->hart().bus().dmem().size());
    if (dialog.exec() != QDialog::Accepted) return;

    write_mem_files(dialog.wants_imem() ? dialog.imem_path() : QString(),
                    dialog.wants_dmem() ? dialog.dmem_path() : QString(), dialog.annotate());
}


}  // namespace rv::gui
