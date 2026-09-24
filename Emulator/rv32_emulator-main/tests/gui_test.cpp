// The desktop interface, driven offscreen.
//
// The same bargain the TUI's headless canvas bought: the window is created for
// real, told to do what a person would do, and then asked what it shows. Qt's
// offscreen platform makes this work with no display, so it runs in CI.
//
// Widgets are driven through MainWindow's public actions rather than by
// synthesising clicks, because the actions are what the toolbar and the menu
// call too -- testing them tests the real path.
#include <QApplication>
#include <QCheckBox>
#include <QListWidget>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QGroupBox>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QToolBar>
#include <QSpinBox>
#include <QTableWidget>

#include "core/device_config.hpp"
#include "gui/device_panel.hpp"
#include "gui/editor_widget.hpp"
#include "gui/encoding_widget.hpp"
#include "gui/pipeline_widget.hpp"
#include "asm/lexer.hpp"
#include "gui/highlighter.hpp"
#include "gui/main_window.hpp"
#include "rv_test.hpp"

using namespace rv;
using namespace rv::gui;

namespace {

constexpr const char* kProgram =
    "        .data\n"
    "msg:    .asciz \"hi\"\n"
    "        .text\n"
    "_start: li   a0, 1\n"
    "        li   a1, 0x12345\n"
    "        add  a2, a0, a1\n"
    "        ebreak\n";

/// A window with a program already loaded and assembled.
struct Fixture {
    MainWindow window;

    explicit Fixture(const char* text = kProgram) {
        window.editor().setPlainText(text);
        window.assemble();
    }

    dbg::DebugSession& session() { return window.session(); }
    EditorWidget& editor() { return window.editor(); }
};

QString register_row(MainWindow& window, RegIdx reg, int column) {
    auto* table = window.findChild<QTableWidget*>();
    if (table == nullptr) return {};
    QTableWidgetItem* item = table->item(reg, column);
    return item == nullptr ? QString{} : item->text();
}

QStringList diagnostic_lines(MainWindow& window) {
    QStringList out;
    if (auto* list = window.findChild<QListWidget*>()) {
        for (int row = 0; row < list->count(); ++row) out << list->item(row)->text();
    }
    return out;
}

/// How far down the pipe the instruction on `line` is, or -1 if that line has
/// nothing in flight.
int depth_of(EditorWidget& editor, int line) {
    for (const PipelineMark& mark : editor.pipeline_lines()) {
        if (mark.line == line) return mark.depth;
    }
    return -1;
}

/// Which way the line's instruction turned out to go, or Pending if the line
/// has nothing in flight.
PipelineTone tone_of(EditorWidget& editor, int line) {
    for (const PipelineMark& mark : editor.pipeline_lines()) {
        if (mark.line == line) return mark.tone;
    }
    return PipelineTone::Pending;
}

/// Advance until the machine lands on a different source line. The window has
/// one step and it is a cycle, so a test that wants "the next line" builds it
/// out of that rather than pretending there is a control for it.
void step_to_next_line(MainWindow& window) {
    const std::optional<u32> start = window.session().current_line();
    for (int guard = 0; guard < 500; ++guard) {
        window.step();
        if (window.session().halted()) return;
        if (window.session().current_line() != start &&
            window.session().map().is_line_start(window.session().cpu().pc)) {
            return;
        }
    }
}

bool has(const QStringList& lines, const char* needle) {
    for (const QString& line : lines) {
        if (line.contains(needle)) return true;
    }
    return false;
}

}  // namespace

RV_TEST(gui, a_program_assembles_and_loads) {
    Fixture fixture;
    RV_CHECK(fixture.session().has_program());
    RV_CHECK_EQ(fixture.session().program().imem_words.size(), std::size_t{5});
    RV_CHECK(diagnostic_lines(fixture.window).isEmpty());
}

RV_TEST(gui, stepping_updates_the_register_table) {
    Fixture fixture;
    RV_CHECK_STR(register_row(fixture.window, 10, 1).toStdString(), "00000000");

    step_to_next_line(fixture.window);
    RV_CHECK_STR(register_row(fixture.window, 10, 1).toStdString(), "00000001");
    // Both spellings, because the encoding holds the number and assembly is
    // written with the name.
    RV_CHECK_STR(register_row(fixture.window, 10, 0).toStdString(), "x10  a0");
    RV_CHECK_STR(register_row(fixture.window, 10, 2).toStdString(), "1");
}

RV_TEST(gui, the_changed_register_is_marked_rather_than_merely_updated) {
    Fixture fixture;
    step_to_next_line(fixture.window);

    auto* table = fixture.window.findChild<QTableWidget*>();
    RV_CHECK(table != nullptr);
    // a0 just changed, so it is bold; a1 did not, so it is not.
    RV_CHECK(table->item(10, 1)->font().bold());
    RV_CHECK(!table->item(11, 1)->font().bold());
}

RV_TEST(gui, the_execution_arrow_points_at_what_wrote_back) {
    // Not at the pc: by the time an instruction retires the pc is four stages
    // further on, and an arrow there would point at code that has not run.
    Fixture fixture;
    RV_CHECK_EQ(fixture.editor().execution_line(), 4);  // nothing has run yet

    // Five cycles retires the first instruction, on line 4.
    for (int i = 0; i < 5; ++i) fixture.window.step();
    RV_CHECK_EQ(fixture.editor().execution_line(), 4);
    RV_CHECK_EQ(fixture.session().current_line().value_or(0), 5u);  // the pc is ahead

    fixture.window.step();
    RV_CHECK_EQ(fixture.editor().execution_line(), 5);
}

RV_TEST(gui, the_encoding_panel_explains_what_wrote_back) {
    // The panel says what an instruction did, so it has to be looking at the
    // one that did it -- the instruction in write-back, not the one at the pc.
    Fixture fixture;
    auto* encoding = fixture.window.findChild<EncodingWidget*>();
    RV_CHECK(encoding != nullptr);

    // `_start: li a0, 1` is line 4 and assembles to a single addi.
    for (int i = 0; i < 5; ++i) fixture.window.step();
    const QStringList fields = encoding->field_summary();
    RV_CHECK(has(fields, "rd=x10 (a0)"));
    RV_CHECK(has(fields, "imm[11:0]=1"));
}

RV_TEST(gui, stepping_by_cycle_lands_inside_a_pseudo) {
    // `li a1, 0x12345` is really two instructions, and cycling lands on the
    // seam without being asked to: one cycle past the end of line 4 puts the
    // pc on the second half of line 5.
    Fixture fixture;
    step_to_next_line(fixture.window);  // onto the li

    fixture.window.step();
    RV_CHECK_EQ(fixture.session().current_line().value_or(0), 5u);
    const auto slot = fixture.session().current_slot();
    RV_CHECK(slot.has_value());
    RV_CHECK_EQ(slot->first, 1);
    RV_CHECK_EQ(slot->second, 2);
    RV_CHECK_NE(fixture.window.status_text().indexOf("[2/2]"), -1);
}

RV_TEST(gui, breakpoints_toggle_and_stop_a_run) {
    Fixture fixture;
    fixture.window.toggle_breakpoint(6);
    RV_CHECK_EQ(fixture.session().breakpoints().breakpoints().size(), std::size_t{1});

    // run() starts a timer; drive one slice directly, which is what it does.
    const auto event = fixture.session().run_chunk(100000);
    RV_CHECK(event.has_value());
    RV_CHECK_EQ(static_cast<int>(event->reason), static_cast<int>(dbg::StopReason::Breakpoint));
    RV_CHECK_EQ(fixture.session().current_line().value_or(0), 6u);

    fixture.window.toggle_breakpoint(6);
    RV_CHECK(fixture.session().breakpoints().breakpoints().empty());
}

RV_TEST(gui, stepping_backwards_undoes_one_cycle) {
    // Back is a cycle, so one press lands *inside* the two-instruction
    // `li a1, 0x12345`: the lui has run and the addi has not, which is the
    // seam the encoding panel has always been about.
    Fixture fixture;
    step_to_next_line(fixture.window);
    step_to_next_line(fixture.window);
    RV_CHECK_STR(register_row(fixture.window, 11, 1).toStdString(), "00012345");

    fixture.window.step_back();
    RV_CHECK_STR(register_row(fixture.window, 11, 1).toStdString(), "00012000");

    // And keeping at it walks back out of the line altogether.
    for (int i = 0; i < 12; ++i) {
        if (register_row(fixture.window, 11, 1) == "00000000") break;
        fixture.window.step_back();
    }
    RV_CHECK_STR(register_row(fixture.window, 11, 1).toStdString(), "00000000");
}

RV_TEST(gui, a_failed_assemble_lists_the_errors_and_keeps_the_text) {
    Fixture fixture;
    fixture.editor().setPlainText(
        "_start: nop\n"
        "        addii a0, a1, 5\n"
        "        ebreak\n");
    fixture.window.assemble();

    const QStringList lines = diagnostic_lines(fixture.window);
    RV_CHECK_EQ(lines.size(), 1);
    RV_CHECK(has(lines, "unknown instruction"));
    RV_CHECK(has(lines, "did you mean 'addi'"));  // the hint reaches the list

    // The buffer is untouched and the caret is on the problem.
    RV_CHECK_NE(fixture.editor().toPlainText().indexOf("addii"), -1);
    RV_CHECK_EQ(fixture.editor().current_line(), 2);
    RV_CHECK(fixture.editor().diagnostic_on_line(2) != nullptr);
}

RV_TEST(gui, a_failed_assemble_leaves_the_loaded_program_alone) {
    // Assembling a detached copy means a broken edit never destroys the program
    // that is currently steppable.
    Fixture fixture;
    RV_CHECK_EQ(fixture.session().program().imem_words.size(), std::size_t{5});

    fixture.editor().setPlainText("        addii bad\n");
    fixture.window.assemble();
    RV_CHECK_EQ(fixture.session().program().imem_words.size(), std::size_t{5});
}

RV_TEST(gui, the_harvard_error_reaches_the_diagnostics_list) {
    Fixture fixture;
    fixture.editor().setPlainText(
        "        .data\n"
        "msg:    .asciz \"x\"\n"
        "        .text\n"
        "_start: call msg\n");
    fixture.window.assemble();

    const QStringList lines = diagnostic_lines(fixture.window);
    RV_CHECK(has(lines, "E0310"));
    RV_CHECK(has(lines, "data symbol"));
}

RV_TEST(gui, the_encoding_panel_names_the_fields_of_the_current_instruction) {
    Fixture fixture;
    auto* encoding = fixture.window.findChild<EncodingWidget*>();
    RV_CHECK(encoding != nullptr);

    // The add on line 6.
    const auto& addrs = fixture.session().map().addrs_for_line(6);
    RV_CHECK(!addrs.empty());
    encoding->set_address(addrs.front());

    const QStringList fields = encoding->field_summary();
    RV_CHECK(has(fields, "funct7"));
    RV_CHECK(has(fields, "rs1=x10 (a0)"));
    RV_CHECK(has(fields, "rs2=x11 (a1)"));
    RV_CHECK(has(fields, "rd=x12 (a2)"));
    RV_CHECK(has(fields, "opcode="));
}

RV_TEST(gui, the_encoding_panel_names_the_scrambled_branch_immediate) {
    // The B-type immediate is the one everybody gets wrong; naming the pieces
    // is the point of the panel.
    Fixture fixture(
        "here:   beq a0, a1, here\n"
        "        ebreak\n");
    auto* encoding = fixture.window.findChild<EncodingWidget*>();
    encoding->set_address(0);

    const QStringList fields = encoding->field_summary();
    RV_CHECK(has(fields, "imm[12|10:5]"));
    RV_CHECK(has(fields, "imm[4:1|11]"));
}

RV_TEST(gui, the_encoding_panel_renders_a_compressed_instruction_at_16_bits) {
    // The panel is driven by InstrFormat, so the compressed layouts are a
    // different set of field tables rather than the 32-bit one with bits
    // hidden. The op field being two bits wide is the giveaway that the right
    // one was picked.
    Fixture fixture(
        "        c.li a0, 5\n"
        "        ebreak\n");
    auto* encoding = fixture.window.findChild<EncodingWidget*>();
    encoding->set_address(0);

    const QStringList fields = encoding->field_summary();
    RV_CHECK(has(fields, "funct3"));
    RV_CHECK(has(fields, "rd/rs1=x10 (a0)"));
    RV_CHECK(has(fields, "imm[4:0]=5"));
    RV_CHECK(has(fields, "op=0x1 c.li"));
    // The 32-bit layout must not have been used: there is no funct7 in 16 bits.
    RV_CHECK(!has(fields, "funct7"));
}

RV_TEST(gui, the_encoding_panel_names_the_scattered_compressed_offsets) {
    // c.lw and c.swsp both encode a word offset and scatter it differently.
    // Naming which bits go where is the only way that is ever visible.
    Fixture fixture(
        "        c.lw a0, 8(a1)\n"
        "        c.swsp a0, 8(sp)\n"
        "        ebreak\n");
    auto* encoding = fixture.window.findChild<EncodingWidget*>();

    encoding->set_address(0);
    const QStringList lw = encoding->field_summary();
    RV_CHECK(has(lw, "off[5:3]"));
    RV_CHECK(has(lw, "off[2|6]"));
    RV_CHECK(has(lw, "rs1'=x11 (a1)"));

    encoding->set_address(2);
    const QStringList swsp = encoding->field_summary();
    RV_CHECK(has(swsp, "off[5:2|7:6]"));
    RV_CHECK(has(swsp, "rs2=x10 (a0)"));
}

RV_TEST(gui, a_compressed_instruction_in_the_last_halfword_of_imem_still_decodes) {
    // read_imem_word would return 0 here, because the word it wants runs off
    // the end. The panel has to fetch the way the hart does.
    Fixture fixture(
        "        c.li a0, 5\n"
        "        ebreak\n");
    auto* encoding = fixture.window.findChild<EncodingWidget*>();
    const Addr last_half = fixture.session().hart().imem().size() - 2;
    encoding->set_address(last_half);
    // Nothing is loaded there, so it decodes as an illegal halfword rather
    // than crashing or silently showing the wrong instruction.
    RV_CHECK(encoding->field_summary().isEmpty());

    encoding->set_address(0);
    RV_CHECK(has(encoding->field_summary(), "op=0x1 c.li"));
}

RV_TEST(gui, the_encoding_panel_follows_the_caret_before_anything_runs) {
    Fixture fixture;
    auto* encoding = fixture.window.findChild<EncodingWidget*>();

    fixture.editor().go_to_line(6);  // the add
    RV_CHECK(has(encoding->field_summary(), "opcode=0x33 add"));

    fixture.editor().go_to_line(4);  // the first li
    RV_CHECK(has(encoding->field_summary(), "opcode=0x13 addi"));
}

RV_TEST(gui, the_status_line_says_whether_the_buffer_has_been_built) {
    Fixture fixture;
    RV_CHECK_EQ(fixture.window.status_text().indexOf("not assembled"), -1);

    fixture.editor().insertPlainText("        nop\n");
    // The status only refreshes on the events that matter; stepping is one.
    step_to_next_line(fixture.window);
    RV_CHECK_NE(fixture.window.status_text().indexOf("not assembled"), -1);
}

RV_TEST(gui, reset_keeps_breakpoints_and_restores_data) {
    Fixture fixture(
        "        .data\n"
        "slot:   .word 0x1234\n"
        "        .text\n"
        "_start: la a1, slot\n"
        "        sw zero, 0(a1)\n"
        "        ebreak\n");
    fixture.window.toggle_breakpoint(5);
    step_to_next_line(fixture.window);
    step_to_next_line(fixture.window);
    RV_CHECK_HEX(fixture.session().read_dmem_word(0), 0u);

    fixture.window.reset_machine();
    RV_CHECK_HEX(fixture.session().read_dmem_word(0), 0x1234u);
    RV_CHECK_EQ(fixture.session().breakpoints().breakpoints().size(), std::size_t{1});
    RV_CHECK(!fixture.session().can_reverse());
}

RV_TEST(gui, interrupts_are_visible_in_the_io_panel) {
    Fixture fixture(
        "        .equ MTIMECMP, 0xffff0040\n"
        "_start: li t0, MTIMECMP\n"
        "        li t1, 50\n"
        "        sw t1, 0(t0)\n"
        "        ebreak\n");
    for (int i = 0; i < 3; ++i) step_to_next_line(fixture.window);

    RV_CHECK(fixture.session().hart().bus().timer()->armed());
    RV_CHECK_HEX(fixture.session().hart().bus().timer()->compare(), 50u);
}

RV_TEST(gui, the_device_panel_lists_what_is_attached) {
    Fixture fixture;
    auto* panel = fixture.window.findChild<DevicePanel*>();
    RV_CHECK(panel != nullptr);

    auto* list = panel->findChild<QTableWidget*>();
    RV_CHECK(list != nullptr);
    RV_CHECK_EQ(list->rowCount(),
                static_cast<int>(fixture.session().hart().bus().devices().size()));

    // The address is what a program needs, so it is in the table rather than
    // left to be worked out from the slot.
    bool found_uart = false;
    for (int row = 0; row < list->rowCount(); ++row) {
        if (list->item(row, 3)->text() == "uart") {
            found_uart = true;
            RV_CHECK_STR(list->item(row, 1)->text().toStdString(), "0xffff0000");
        }
    }
    RV_CHECK(found_uart);
}

RV_TEST(gui, adding_and_removing_a_peripheral_updates_the_machine) {
    Fixture fixture;
    core::Bus& bus = fixture.session().hart().bus();
    const std::size_t before = bus.devices().size();

    bus.attach(9, core::make_device("button"));
    RV_CHECK_EQ(bus.devices().size(), before + 1);
    RV_CHECK(bus.device_at_slot(9) != nullptr);

    // The panel picks it up on the next refresh, which every step triggers.
    step_to_next_line(fixture.window);
    auto* list = fixture.window.findChild<DevicePanel*>()->findChild<QTableWidget*>();
    RV_CHECK_EQ(list->rowCount(), static_cast<int>(before + 1));

    bus.detach(9);
    step_to_next_line(fixture.window);
    RV_CHECK_EQ(list->rowCount(), static_cast<int>(before));
}

RV_TEST(gui, memory_can_be_edited_by_hand) {
    // Typing a value into the memory table writes it, using the debugger's
    // path rather than the program's -- so editing a device register does not
    // trigger whatever a write from the program would do.
    Fixture fixture;
    auto* table = fixture.window.findChild<QTableWidget*>("");
    // The memory table is the one with an "address" header.
    QTableWidget* memory = nullptr;
    for (QTableWidget* candidate : fixture.window.findChildren<QTableWidget*>()) {
        if (candidate->columnCount() == 5 &&
            candidate->horizontalHeaderItem(0)->text() == "address" &&
            candidate->rowCount() > 100) {  // data memory, not a block's view
            memory = candidate;
        }
    }
    (void)table;
    RV_CHECK(memory != nullptr);
    if (memory == nullptr) return;

    RV_CHECK_HEX(fixture.session().read_dmem_word(0x10), 0u);
    memory->item(1, 1)->setText("deadbeef");  // row 1 is address 0x10, column 1 is +0
    RV_CHECK_HEX(fixture.session().read_dmem_word(0x10), 0xdeadbeefu);
}

RV_TEST(gui, operating_a_switch_does_not_destroy_the_control_operating_it) {
    // The crash this is here for: a control's handler called refresh(), which
    // rebuilt the controls, which deleted the very checkbox whose toggled()
    // signal was still on the stack. Qt then returned into freed memory.
    Fixture fixture;
    auto* panel = fixture.window.findChild<DevicePanel*>();
    RV_CHECK(panel != nullptr);
    if (panel == nullptr) return;

    QTableWidget* list = panel->findChild<QTableWidget*>();
    RV_CHECK(list != nullptr);
    if (list == nullptr) return;

    int switches_row = -1;
    for (int row = 0; row < list->rowCount(); ++row) {
        if (list->item(row, 3)->text() == "switches") switches_row = row;
    }
    RV_CHECK_NE(switches_row, -1);
    if (switches_row < 0) return;
    list->selectRow(switches_row);

    const QList<QCheckBox*> boxes = panel->findChildren<QCheckBox*>();
    RV_CHECK_EQ(boxes.size(), 32);
    if (boxes.isEmpty()) return;

    // Bit 0 is the last one, since the row is painted bit 31 first.
    QPointer<QCheckBox> bit0 = boxes.back();
    bit0->setChecked(true);

    RV_CHECK(!bit0.isNull());  // it used to be deleted here, under its own signal
    RV_CHECK_HEX(fixture.session().hart().bus().switches()->value(), 1u);

    // And it still works the second time, which the dangling one did not.
    bit0->setChecked(false);
    RV_CHECK(!bit0.isNull());
    RV_CHECK_HEX(fixture.session().hart().bus().switches()->value(), 0u);
}

RV_TEST(gui, a_step_does_not_rebuild_the_controls_underneath_the_user) {
    // refresh() runs after every step. Tearing the controls down each time
    // would take the focus out of whatever is being operated -- and put a
    // deleted widget under the mouse.
    Fixture fixture;
    auto* panel = fixture.window.findChild<DevicePanel*>();
    QTableWidget* list = panel->findChild<QTableWidget*>();
    for (int row = 0; row < list->rowCount(); ++row) {
        if (list->item(row, 3)->text() == "switches") list->selectRow(row);
    }
    QPointer<QCheckBox> box = panel->findChildren<QCheckBox*>().back();
    RV_CHECK(!box.isNull());

    step_to_next_line(fixture.window);
    step_to_next_line(fixture.window);
    RV_CHECK(!box.isNull());
}

RV_TEST(gui, a_device_configuration_round_trips_through_the_panel) {
    Fixture fixture;
    core::Bus& bus = fixture.session().hart().bus();
    bus.switches()->set_value(0x1234);

    const core::DeviceConfig described = core::describe(bus);
    const std::string text = core::write_device_config(described);
    RV_CHECK_NE(text.find("[[device]]"), std::string::npos);
    RV_CHECK_NE(text.find("\"switches\""), std::string::npos);

    const core::DeviceConfig reparsed = core::parse_device_config(text, "t.toml");
    RV_CHECK(reparsed.ok());
    RV_CHECK_EQ(reparsed.devices.size(), described.devices.size());
}

RV_TEST(gui, an_overlapping_device_is_allowed_and_shown_as_covered) {
    // Refusing would stop the work; hiding it would be worse. It goes on, and
    // the list says which one stopped answering.
    Fixture fixture;
    core::Bus& bus = fixture.session().hart().bus();
    bus.detach_all();
    core::Device* ram = bus.attach(0, core::make_device("ram", 64));
    core::Device* leds = bus.attach(0, core::make_device("leds"));
    RV_CHECK(ram != nullptr);
    RV_CHECK(leds != nullptr);

    step_to_next_line(fixture.window);
    auto* panel = fixture.window.findChild<DevicePanel*>();
    QTableWidget* list = panel->findChild<QTableWidget*>();
    RV_CHECK_EQ(list->rowCount(), 2);

    // The block is only partly covered -- three of its four slots still answer.
    RV_CHECK_NE(list->item(0, 4)->text().indexOf("partly covered"), -1);
    // The one on top reports normally -- it is the one answering.
    RV_CHECK_EQ(list->item(1, 4)->text().indexOf("covered"), -1);
}

RV_TEST(gui, the_memory_list_resizes_the_machine) {
    // On the memory tab, because that is what the table is a view of: changing
    // one while looking at the other is the shortest way to see what the
    // number means.
    Fixture fixture;
    fixture.window.show_tab("Memory");
    QList<QComboBox*> boxes = fixture.window.findChildren<QComboBox*>();
    RV_CHECK_EQ(boxes.size(), 2);
    if (boxes.size() != 2) return;

    RV_CHECK_NE(boxes[0]->currentText().indexOf("× 32-bit"), -1);

    const int eight_kb = boxes[0]->findData(QVariant(uint{8192}));
    RV_CHECK_NE(eight_kb, -1);
    boxes[0]->setCurrentIndex(eight_kb);
    RV_CHECK_EQ(fixture.session().hart().imem().size(), 8192u);

    const int four_kb = boxes[1]->findData(QVariant(uint{4096}));
    boxes[1]->setCurrentIndex(four_kb);
    RV_CHECK_EQ(fixture.session().hart().bus().dmem().size(), 4096u);
}

RV_TEST(gui, a_size_a_file_names_is_shown_even_when_the_list_lacks_it) {
    // The alternative is a box that says one size while the machine has
    // another, which is worse than an entry that is not a round number.
    Fixture fixture;
    fixture.session().hart().resize_memories(5120, 16 * 1024);
    fixture.window.show_tab("Memory");
    step_to_next_line(fixture.window);

    QComboBox* imem = fixture.window.findChildren<QComboBox*>().front();
    RV_CHECK_EQ(imem->currentData().toUInt(), 5120u);
    RV_CHECK_NE(imem->currentText().indexOf("1280 × 32-bit"), -1);
}

RV_TEST(gui, a_block_and_a_custom_peripheral_get_fields_to_type_into) {
    Fixture fixture;
    core::Bus& bus = fixture.session().hart().bus();
    bus.detach_all();
    bus.attach(0, core::make_device("custom", 3, "sensor"));

    auto* panel = fixture.window.findChild<DevicePanel*>();
    panel->refresh();
    QTableWidget* list = panel->findChild<QTableWidget*>();
    list->selectRow(0);

    // Inside the Controls box: the memory size fields are spin boxes elsewhere
    // in the panel, and each of those owns a line edit of its own.
    auto* controls = panel->findChild<QGroupBox*>();
    RV_CHECK(controls != nullptr);
    if (controls == nullptr) return;

    // Three registers to type into, and the interrupt line as a switch.
    RV_CHECK_EQ(controls->findChildren<QLineEdit*>().size(), 3);
    RV_CHECK_EQ(controls->findChildren<QCheckBox*>().size(), 1);

    // Typing into one is what the program then reads.
    QLineEdit* first = controls->findChildren<QLineEdit*>().front();
    first->setText("0x2a");
    emit first->editingFinished();
    RV_CHECK_EQ(bus.load(core::slot_address(0), 4, false).value, 42u);
}

RV_TEST(gui, a_peripheral_that_computes_offers_only_its_operands) {
    Fixture fixture;
    core::Bus& bus = fixture.session().hart().bus();
    bus.detach_all();
    core::Device* mul = bus.attach(0, core::make_device("function", 8, "multiplier"));
    RV_CHECK(mul != nullptr);
    if (mul == nullptr) return;

    auto* panel = fixture.window.findChild<DevicePanel*>();
    panel->refresh();
    QTableWidget* list = panel->findChild<QTableWidget*>();
    list->selectRow(0);

    auto* controls = panel->findChild<QGroupBox*>();
    RV_CHECK(controls != nullptr);
    if (controls == nullptr) return;

    // Two operands and nothing else: the result and the status are computed,
    // so there is nothing there for a person to set.
    const QList<QLineEdit*> fields = controls->findChildren<QLineEdit*>();
    RV_CHECK_EQ(fields.size(), 2);
    if (fields.size() != 2) return;

    fields[0]->setText("12");
    emit fields[0]->editingFinished();
    fields[1]->setText("12");
    emit fields[1]->editingFinished();
    RV_CHECK_EQ(bus.load(core::slot_address(0), 4, false).value, 12u);

    // The panel names it and says what it is holding, like any other device:
    // columns are slot, address, name, type, state.
    panel->refresh();
    RV_CHECK_STR(list->item(0, 2)->text().toStdString(), "multiplier");
    RV_CHECK_STR(list->item(0, 3)->text().toStdString(), "function");
    // The operands it now holds, and idle -- typing into a field is the
    // person's path, so it loads the register without starting the peripheral.
    // Only a store from the program does that.
    RV_CHECK_NE(list->item(0, 4)->text().indexOf("a=12 b=12"), -1);
    RV_CHECK_NE(list->item(0, 4)->text().indexOf("idle"), -1);
}

RV_TEST(gui, the_exported_images_are_as_deep_as_the_machine) {
    // The whole point of the tool: the file goes into a block memory of a
    // fixed depth, so the file has to be that many lines whatever the program
    // is. Getting it short is the bug that reads as the program going wrong.
    Fixture fixture;
    fixture.session().hart().resize_memories(4096, 2048);

    const QString folder = QDir::tempPath() + "/rv32_export_test";
    QDir().mkpath(folder);
    const QString imem = folder + "/t_imem.mem";
    const QString dmem = folder + "/t_dmem.mem";
    QFile::remove(imem);
    QFile::remove(dmem);

    RV_CHECK(fixture.window.write_mem_files(imem, dmem, /*annotate=*/false));

    const auto line_count = [](const QString& path) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return -1;
        return static_cast<int>(QString::fromUtf8(file.readAll()).count(QLatin1Char('\n')));
    };
    RV_CHECK_EQ(line_count(imem), 1024);  // 4096 bytes / 4
    RV_CHECK_EQ(line_count(dmem), 512);   // 2048 bytes / 4

    QFile::remove(imem);
    QFile::remove(dmem);
    QDir().rmdir(folder);
}

RV_TEST(gui, generate_sits_next_to_assemble) {
    // The other half of the same sentence: one turns the text into a machine,
    // the other turns the machine into files a synthesis tool will take.
    Fixture fixture;
    QStringList toolbar;
    for (QToolBar* bar : fixture.window.findChildren<QToolBar*>()) {
        for (QAction* action : bar->actions()) {
            if (!action->isSeparator()) toolbar << action->text();
        }
    }
    RV_CHECK_EQ(toolbar.indexOf("Generate"), toolbar.indexOf("Assemble") + 1);
}

RV_TEST(gui, a_block_exports_itself_at_its_own_depth) {
    // Its contents are a memory image in their own right -- a table, a font, a
    // sine wave -- and what is wanted on the far side is that block in a BRAM
    // of its own, not the program's data memory with the block inside it.
    Fixture fixture;
    core::Bus& bus = fixture.session().hart().bus();
    bus.detach_all();
    auto* ram = static_cast<core::RamDevice*>(bus.attach(0, core::make_device("ram", 64)));
    ram->set_input(0, 0xdeadbeef);

    auto* panel = fixture.window.findChild<DevicePanel*>();
    panel->refresh();

    const QString path = QDir::tempPath() + "/rv32_block_test.mem";
    QFile::remove(path);
    RV_CHECK(panel->write_block(0, path));

    QFile file(path);
    RV_CHECK(file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QStringList lines =
        QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    // 64 bytes is sixteen words, and the file is sixteen lines whatever is in
    // it -- that is what makes it loadable into a block of that depth.
    RV_CHECK_EQ(lines.size(), 16);
    RV_CHECK_STR(lines.front().toStdString(), "deadbeef");
    RV_CHECK_STR(lines.back().toStdString(), "00000000");
    file.close();
    QFile::remove(path);
}

RV_TEST(gui, instructions_past_the_end_of_imem_are_marked_not_cut) {
    // load_words truncates silently, so a program too big for the machine used
    // to become a different program without saying so. It is marked instead:
    // what was written is still what is there.
    QString source = "        .text\n_start:\n";
    for (int i = 0; i < 80; ++i) source += "        addi    a0, a0, 1\n";

    Fixture fixture;
    fixture.session().hart().resize_memories(256, 256);  // 64 words
    fixture.editor().setPlainText(source);
    fixture.window.assemble();

    // Line 3 is the first instruction, so lines 3..66 fit and 67 onwards do not.
    const std::vector<int>& marked = fixture.editor().overflow_lines();
    RV_CHECK_EQ(marked.size(), std::size_t{16});
    RV_CHECK_EQ(marked.front(), 67);
    RV_CHECK_EQ(marked.back(), 82);

    // And the program is left whole rather than trimmed to what fits.
    RV_CHECK_EQ(fixture.session().program().imem_words.size(), std::size_t{80});

    // A bigger machine makes it go away -- both ends of the comparison move.
    fixture.session().hart().resize_memories(1024, 256);
    fixture.window.assemble();
    RV_CHECK(fixture.editor().overflow_lines().empty());
}

RV_TEST(gui, data_past_the_end_of_dmem_is_reported_even_with_no_line_to_mark) {
    // .data sits in its own space and the source map covers instructions, so
    // there is no line to paint -- which is a reason to say it elsewhere, not
    // a reason to say nothing.
    Fixture fixture;
    fixture.session().hart().resize_memories(4096, 256);
    fixture.editor().setPlainText(
        "        .data\n"
        "buffer: .zero 512\n"
        "        .text\n"
        "_start: ebreak\n");
    fixture.window.assemble();

    RV_CHECK(fixture.editor().overflow_lines().empty());
    RV_CHECK_NE(fixture.window.message().indexOf("past the end of dmem"), -1);
}

RV_TEST(gui, a_device_can_be_renamed_after_it_exists) {
    // What two of a kind should be called is usually clear only once both are
    // there, so naming cannot be confined to the moment of adding.
    Fixture fixture;
    core::Bus& bus = fixture.session().hart().bus();
    bus.detach_all();
    bus.attach(0, core::make_device("uart"));

    auto* panel = fixture.window.findChild<DevicePanel*>();
    panel->refresh();
    QTableWidget* list = panel->findChild<QTableWidget*>();
    // A name and a type are different things, so they are different columns.
    RV_CHECK_STR(list->horizontalHeaderItem(2)->text().toStdString(), "name");
    RV_CHECK_STR(list->horizontalHeaderItem(3)->text().toStdString(), "type");
    RV_CHECK(list->item(0, 2)->flags().testFlag(Qt::ItemIsEditable));
    RV_CHECK(!list->item(0, 3)->flags().testFlag(Qt::ItemIsEditable));
    RV_CHECK(list->item(0, 2)->text().isEmpty());
    RV_CHECK_STR(list->item(0, 3)->text().toStdString(), "uart");

    list->item(0, 2)->setText("console");
    RV_CHECK_STR(bus.device_at_slot(0)->display_name(), "console");
    RV_CHECK_STR(list->item(0, 3)->text().toStdString(), "uart");  // still says what it is

    list->item(0, 2)->setText("");
    RV_CHECK(bus.device_at_slot(0)->label().empty());
}

RV_TEST(gui, a_block_is_looked_at_where_it_is_configured) {
    // Under the device it belongs to, in the same shape the memory tab uses
    // for data memory. Sixteen numbered fields were a list of words pretending
    // not to be memory.
    Fixture fixture;
    core::Bus& bus = fixture.session().hart().bus();
    bus.detach_all();
    auto* ram = static_cast<core::RamDevice*>(bus.attach(0, core::make_device("ram", 64)));
    ram->poke(4, 0xdeadbeef);

    auto* panel = fixture.window.findChild<DevicePanel*>();
    panel->refresh();
    panel->findChild<QTableWidget*>()->selectRow(0);

    // The controls hold a hex view of the block, four words to the row.
    QTableWidget* view = nullptr;
    for (QTableWidget* candidate : panel->findChild<QGroupBox*>()->findChildren<QTableWidget*>()) {
        view = candidate;
    }
    RV_CHECK(view != nullptr);
    if (view == nullptr) return;
    RV_CHECK_EQ(view->rowCount(), 4);  // 64 bytes, sixteen to a row
    RV_CHECK_STR(view->item(0, 0)->text().toStdString(), "ffff0000");
    RV_CHECK_STR(view->item(0, 2)->text().toStdString(), "deadbeef");

    // And typing into it writes the block.
    view->item(0, 1)->setText("00c0ffee");
    RV_CHECK_HEX(ram->peek(0), 0x00c0ffeeu);
}

RV_TEST(gui, a_block_exports_at_its_own_depth) {
    Fixture fixture;
    core::Bus& bus = fixture.session().hart().bus();
    bus.detach_all();
    auto* ram = static_cast<core::RamDevice*>(bus.attach(0, core::make_device("ram", 64)));
    ram->poke(0, 0xdeadbeef);

    auto* panel = fixture.window.findChild<DevicePanel*>();
    const QString path = QDir::tempPath() + "/rv32_block_depth.mem";
    QFile::remove(path);
    RV_CHECK(panel->write_block(0, path));

    QFile file(path);
    RV_CHECK(file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QStringList lines =
        QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    RV_CHECK_EQ(lines.size(), 16);  // the block, not data memory
    RV_CHECK_STR(lines.front().toStdString(), "deadbeef");
    file.close();
    QFile::remove(path);
}

RV_TEST(gui, a_bank_is_offered_at_the_widths_hardware_comes_in) {
    // Eight LEDs, sixteen switches -- what a board actually has. The catalogue
    // says what unit a type is counted in, so the dialog does not have to know
    // that leds are bits and ram is bytes.
    RV_CHECK(core::find_device_type("leds")->unit == core::SizeUnit::Bits);
    RV_CHECK(core::find_device_type("ram")->unit == core::SizeUnit::Bytes);
    RV_CHECK(core::find_device_type("custom")->unit == core::SizeUnit::Words);
    RV_CHECK(core::find_device_type("uart")->unit == core::SizeUnit::None);

    Fixture fixture;
    core::Bus& bus = fixture.session().hart().bus();
    bus.detach_all();
    auto* leds = static_cast<core::LedDevice*>(bus.attach(0, core::make_device("leds", 8)));
    RV_CHECK_EQ(leds->width(), 8);

    step_to_next_line(fixture.window);
    QTableWidget* list = fixture.window.findChild<DevicePanel*>()->findChild<QTableWidget*>();
    // Eight lights shown, not thirty-two that do not exist.
    RV_CHECK_EQ(list->item(0, 4)->text().trimmed().size(), QString("00000000  ........").size());
}

RV_TEST(gui, typing_ad_offers_add_and_addi_in_the_editor) {
    // The request, end to end: type two characters and the popup appears with
    // the instructions that start with them.
    Fixture fixture;
    fixture.editor().setPlainText("        ad");
    QTextCursor cursor = fixture.editor().textCursor();
    cursor.movePosition(QTextCursor::End);
    fixture.editor().setTextCursor(cursor);

    fixture.editor().request_completion();
    const QStringList offered = fixture.editor().visible_completions();
    RV_CHECK(offered.contains("add"));
    RV_CHECK(offered.contains("addi"));
    RV_CHECK(!offered.contains("sub"));
}

RV_TEST(gui, the_operands_still_to_write_are_greyed_in_after_the_caret) {
    Fixture fixture;
    fixture.editor().setPlainText("");

    const auto ghost_after = [&](const char* line) {
        fixture.editor().setPlainText(QString::fromLatin1(line));
        QTextCursor cursor = fixture.editor().textCursor();
        cursor.movePosition(QTextCursor::End);
        fixture.editor().setTextCursor(cursor);
        return fixture.editor().ghost_text().toStdString();
    };

    // An empty field asks for itself; the ones behind it wait their turn.
    RV_CHECK_STR(ghost_after("        addi "), "rd, rs1, imm");
    RV_CHECK_STR(ghost_after("        addi a0, "), "rs1, imm");
    RV_CHECK_STR(ghost_after("        addi a0, a1, "), "imm(-2048..2047)");
    // Filled in -- nothing left to say.
    RV_CHECK_STR(ghost_after("        addi a0, a1, 4"), "");

    // A field being typed is left alone; the comma to the next one is offered.
    RV_CHECK_STR(ghost_after("        addi a0"), ", rs1, imm");

    RV_CHECK_STR(ghost_after("        lw a0, "), "offset(base)");
    RV_CHECK_STR(ghost_after("        beq a0, a1, "), "label");

    // Not while the mnemonic itself is still being written -- that is the
    // completion popup's half of the job.
    RV_CHECK_STR(ghost_after("        ad"), "");
}

RV_TEST(gui, the_ghost_keeps_out_of_the_way_of_real_text) {
    Fixture fixture;
    fixture.editor().setPlainText("        addi a0, a1, 4");

    // Caret parked after `addi`, with operands already written past it. The
    // ghost would be drawn straight over them, so there is none.
    QTextCursor cursor = fixture.editor().textCursor();
    cursor.setPosition(fixture.editor().document()->firstBlock().position() + 12);
    fixture.editor().setTextCursor(cursor);
    RV_CHECK_STR(fixture.editor().ghost_text().toStdString(), "");
}

RV_TEST(gui, the_bar_folds_away_once_the_instruction_is_settled) {
    // The bar is for choosing the instruction. In an operand position the
    // ghost has already answered, so a list there would be a second answer to
    // the same question -- and it would cost the editor a line to say it.
    Fixture fixture;
    fixture.editor().setPlainText("        ad");
    QTextCursor cursor = fixture.editor().textCursor();
    cursor.movePosition(QTextCursor::End);
    fixture.editor().setTextCursor(cursor);
    fixture.editor().request_completion();
    RV_CHECK(fixture.editor().suggestion_bar()->is_open());

    fixture.editor().setPlainText("        addi a0, a");
    cursor = fixture.editor().textCursor();
    cursor.movePosition(QTextCursor::End);
    fixture.editor().setTextCursor(cursor);
    fixture.editor().request_completion();
    RV_CHECK(!fixture.editor().suggestion_bar()->is_open());
    RV_CHECK_STR(fixture.editor().ghost_text().toStdString(), ", imm(-2048..2047)");
}

RV_TEST(gui, the_block_sits_under_the_caret_and_not_across_the_editor) {
    // It follows the word it is about, and it is sized to its candidates: a
    // full-width strip would be a long way from what it refers to, and it
    // would cost the editor a row every time it opened.
    Fixture fixture;
    fixture.editor().setPlainText("        ad");
    QTextCursor cursor = fixture.editor().textCursor();
    cursor.movePosition(QTextCursor::End);
    fixture.editor().setTextCursor(cursor);
    fixture.editor().request_completion();

    // Within a line or two of the caret -- below it normally, above it when
    // there is no room below -- rather than parked at the edge of the editor.
    const QRect caret = fixture.editor().cursorRect();
    const QRect block = fixture.editor().suggestion_bar()->geometry();
    RV_CHECK(std::abs(block.center().y() - caret.center().y()) <= 2 * block.height());
    RV_CHECK(block.width() < fixture.editor().width() / 2);
}

RV_TEST(gui, tab_walks_along_the_bar_and_lands_in_the_buffer) {
    // Each step is written into the line, not merely highlighted off to the
    // side: the choice is read where the code is.
    Fixture fixture;
    fixture.editor().setPlainText("        ad");
    QTextCursor cursor = fixture.editor().textCursor();
    cursor.movePosition(QTextCursor::End);
    fixture.editor().setTextCursor(cursor);
    fixture.editor().request_completion();

    const QStringList offered = fixture.editor().visible_completions();
    RV_CHECK_EQ(offered.value(0).toStdString(), std::string("add"));

    fixture.editor().cycle_suggestion(+1);
    RV_CHECK_STR(fixture.editor().toPlainText().toStdString(), "        add");
    RV_CHECK(fixture.editor().suggestion_bar()->is_open());  // still cycling

    fixture.editor().cycle_suggestion(+1);
    RV_CHECK_STR(fixture.editor().toPlainText().toStdString(), "        addi");

    // Going past the end comes back round rather than stopping.
    for (int i = 0; i < offered.size(); ++i) fixture.editor().cycle_suggestion(+1);
    RV_CHECK_STR(fixture.editor().toPlainText().toStdString(), "        addi");
}

RV_TEST(gui, settling_on_an_instruction_folds_the_bar_and_starts_the_ghost) {
    Fixture fixture;
    fixture.editor().setPlainText("        ad");
    QTextCursor cursor = fixture.editor().textCursor();
    cursor.movePosition(QTextCursor::End);
    fixture.editor().setTextCursor(cursor);
    fixture.editor().request_completion();
    RV_CHECK(fixture.editor().suggestion_bar()->is_open());

    fixture.editor().insert_completion("addi");
    RV_CHECK_STR(fixture.editor().toPlainText().toStdString(), "        addi ");
    RV_CHECK(!fixture.editor().suggestion_bar()->is_open());
    RV_CHECK_STR(fixture.editor().ghost_text().toStdString(), "rd, rs1, imm");
}

RV_TEST(gui, syntax_roles_come_from_the_assemblers_own_tables) {
    // `addi` is a mnemonic because the instruction table says so, and `addii`
    // stops being one the moment the second i is typed.
    const auto role_of = [](const char* text, std::size_t index) {
        const auto tokens = as::tokenize_for_highlight(text, 1);
        return role_for_token(tokens[index]);
    };
    RV_CHECK_EQ(static_cast<int>(role_of("addi a0, a1, 5", 0)), static_cast<int>(Role::Mnemonic));
    RV_CHECK_EQ(static_cast<int>(role_of("addii a0, a1, 5", 0)), static_cast<int>(Role::Normal));
    RV_CHECK_EQ(static_cast<int>(role_of("addi a0, a1, 5", 1)), static_cast<int>(Role::Register));
    RV_CHECK_EQ(static_cast<int>(role_of("loop:", 0)), static_cast<int>(Role::Label));
    RV_CHECK_EQ(static_cast<int>(role_of(".text", 0)), static_cast<int>(Role::Directive));
    RV_CHECK_EQ(static_cast<int>(role_of("# hi", 0)), static_cast<int>(Role::Comment));
}

int main(int argc, char** argv) {
    // Offscreen so this needs no display, in CI or anywhere else.
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    return rvtest::run_all(argc, argv);
}

// ---------------------------------------------------------------------------
// The pipeline panel
// ---------------------------------------------------------------------------

RV_TEST(gui, the_pipeline_panel_names_the_stage_of_each_instruction) {
    Fixture fixture;
    auto* panel = fixture.window.findChild<PipelineWidget*>();
    RV_CHECK(panel != nullptr);

    // Freshly assembled: nothing is in flight yet, so every stage is a bubble.
    RV_CHECK(has(panel->stage_summary(), "IF=-"));
    RV_CHECK(has(panel->stage_summary(), "EX=-"));

    fixture.window.step();
    RV_CHECK(has(panel->stage_summary(), "IF=addi"));
    RV_CHECK(has(panel->stage_summary(), "ID=-"));

    // Five cycles in, the first instruction has reached write-back, which is
    // where it becomes part of the machine.
    for (int i = 0; i < 4; ++i) fixture.window.step();
    RV_CHECK(has(panel->stage_summary(), "WB=addi"));
    RV_CHECK_EQ(fixture.session().cpu().instret, 1u);
    RV_CHECK_HEX(fixture.session().cpu().x[10], 1u);

    // Actually draw it. stage_summary() reports what the strip says, but the
    // space-time diagram underneath has geometry of its own, and nothing else
    // here would notice it walking off the end of a row.
    RV_CHECK(!panel->grab().isNull());
}

RV_TEST(gui, the_pipeline_panel_draws_a_run_long_enough_to_flush) {
    // A loop, so the diagram has more cycles than columns and at least one
    // branch resolves the other way. Drawing it is the assertion.
    Fixture fixture(
        "_start: li   a0, 4\n"
        "loop:   addi a0, a0, -1\n"
        "        bne  a0, x0, loop\n"
        "        ebreak\n");
    auto* panel = fixture.window.findChild<PipelineWidget*>();
    RV_CHECK(panel != nullptr);

    for (int i = 0; i < 60; ++i) fixture.window.step();
    RV_CHECK(fixture.session().halted());
    RV_CHECK(!panel->grab().isNull());
    // Halted, and everything that was in flight has walked out the far end.
    RV_CHECK(has(panel->stage_summary(), "IF=-"));
    RV_CHECK(has(panel->stage_summary(), "WB=-"));
    RV_CHECK_HEX(fixture.session().cpu().x[10], 0u);
}

RV_TEST(gui, back_cycle_undoes_one_cycle) {
    Fixture fixture;
    for (int i = 0; i < 5; ++i) fixture.window.step();
    RV_CHECK_EQ(fixture.session().cpu().instret, 1u);

    fixture.window.step_back();
    RV_CHECK_EQ(fixture.session().cpu().instret, 0u);

    auto* panel = fixture.window.findChild<PipelineWidget*>();
    RV_CHECK(panel != nullptr);
    RV_CHECK(has(panel->stage_summary(), "WB=-"));
    RV_CHECK(has(panel->stage_summary(), "MEM=addi"));
}

RV_TEST(gui, a_source_line_step_leaves_the_pipeline_showing_what_just_ran) {
    // F8 goes through the session rather than the pipeline, so the panel has to
    // adopt where the machine ended up instead of going blank.
    Fixture fixture;
    step_to_next_line(fixture.window);

    auto* panel = fixture.window.findChild<PipelineWidget*>();
    RV_CHECK(panel != nullptr);
    RV_CHECK(has(panel->stage_summary(), "WB=addi"));
    RV_CHECK(has(panel->stage_summary(), "MEM=lui"));
}

RV_TEST(gui, the_pipeline_tab_is_selectable) {
    Fixture fixture;
    fixture.window.show_tab("Pipeline");
    auto* panel = fixture.window.findChild<PipelineWidget*>();
    RV_CHECK(panel != nullptr);
    RV_CHECK(panel->isVisibleTo(&fixture.window));
}

// ---------------------------------------------------------------------------
// The same pipeline, drawn on the code itself
// ---------------------------------------------------------------------------

RV_TEST(gui, the_editor_tints_the_lines_that_are_in_flight) {
    // kProgram: line 4 is `li a0, 1`, line 5 the two-word `li a1, 0x12345`.
    Fixture fixture;
    RV_CHECK(fixture.window.pipeline_colours());
    RV_CHECK(fixture.editor().pipeline_lines().empty());  // nothing fetched yet

    fixture.window.step();
    RV_CHECK_EQ(fixture.editor().pipeline_lines().size(), std::size_t{1});
    RV_CHECK_EQ(depth_of(fixture.editor(), 4), 0);

    fixture.window.step();
    RV_CHECK_EQ(depth_of(fixture.editor(), 4), 1);
    RV_CHECK_EQ(depth_of(fixture.editor(), 5), 0);
}

RV_TEST(gui, the_tint_darkens_as_the_instruction_moves_down_the_pipe) {
    // One shade per stage, brightest where it was just fetched -- so a line
    // fades out over the five cycles it takes to cross.
    Fixture fixture;
    for (int expected = 0; expected < 5; ++expected) {
        fixture.window.step();
        RV_CHECK_EQ(depth_of(fixture.editor(), 4), expected);
    }
    fixture.window.step();
    RV_CHECK_EQ(depth_of(fixture.editor(), 4), -1);  // out the far end
}

RV_TEST(gui, a_two_word_line_takes_the_stage_of_its_furthest_instruction) {
    // `li a1, 0x12345` is lui + addi. The shading follows the stages strictly,
    // not the source line as a unit: with lui in ID and addi still in IF, the
    // line is at ID, because that is where the pipeline has got to on it.
    Fixture fixture;
    for (int i = 0; i < 3; ++i) fixture.window.step();
    RV_CHECK_EQ(depth_of(fixture.editor(), 5), 1);

    fixture.window.step();
    RV_CHECK_EQ(depth_of(fixture.editor(), 5), 2);
}

RV_TEST(gui, a_taken_branch_turns_its_line_green_once_it_resolves) {
    Fixture fixture(
        "_start: li   a0, 0\n"        // line 1
        "        beq  a0, x0, out\n"  // line 2, taken
        "        li   a1, 99\n"       // line 3
        "out:    ebreak\n");          // line 4

    for (int i = 0; i < 5; ++i) fixture.window.step();
    // In flight, but it has not run yet, so the line claims nothing.
    RV_CHECK_EQ(depth_of(fixture.editor(), 2), 3);
    RV_CHECK(tone_of(fixture.editor(), 2) == PipelineTone::Pending);

    fixture.window.step();  // it writes back, and the answer exists
    RV_CHECK_EQ(depth_of(fixture.editor(), 2), 4);
    RV_CHECK(tone_of(fixture.editor(), 2) == PipelineTone::Taken);
}

RV_TEST(gui, a_branch_that_falls_through_turns_its_line_red) {
    Fixture fixture(
        "_start: li   a0, 1\n"        // line 1
        "        beq  a0, x0, out\n"  // line 2, not taken
        "        li   a1, 99\n"       // line 3, and it really does run
        "out:    ebreak\n");          // line 4

    for (int i = 0; i < 6; ++i) fixture.window.step();
    RV_CHECK(tone_of(fixture.editor(), 2) == PipelineTone::NotTaken);
    // Always-taken guessed wrong, so the fall-through it had thrown away is
    // fetched all over again -- back at the start of the pipe.
    RV_CHECK_EQ(depth_of(fixture.editor(), 3), 0);
}

RV_TEST(gui, an_ordinary_instruction_never_claims_a_direction) {
    Fixture fixture;
    for (int i = 0; i < 4; ++i) fixture.window.step();
    RV_CHECK_EQ(depth_of(fixture.editor(), 4), 3);  // committed, in MEM
    RV_CHECK(tone_of(fixture.editor(), 4) == PipelineTone::Pending);
}

RV_TEST(gui, the_editor_tint_turns_off_without_touching_the_panel) {
    Fixture fixture;
    for (int i = 0; i < 3; ++i) fixture.window.step();
    RV_CHECK(!fixture.editor().pipeline_lines().empty());

    fixture.window.set_pipeline_colours(false);
    RV_CHECK(fixture.editor().pipeline_lines().empty());

    // The panel is the other half of the same feature and keeps working.
    auto* panel = fixture.window.findChild<PipelineWidget*>();
    RV_CHECK(panel != nullptr);
    RV_CHECK(has(panel->stage_summary(), "EX=addi"));

    fixture.window.set_pipeline_colours(true);
    RV_CHECK_EQ(depth_of(fixture.editor(), 4), 2);
}

RV_TEST(gui, a_line_thrown_away_by_the_guess_stops_being_marked_at_once) {
    // The line after a branch is fetched from straight ahead, and dropped as
    // soon as EX computes the target and the guess says otherwise. It must not
    // be left marked on code that never runs.
    Fixture fixture(
        "_start: li   a0, 0\n"       // line 1
        "        beq  a0, x0, out\n" // line 2, taken
        "        li   a1, 99\n"      // line 3, fetched on the wrong path
        "out:    ebreak\n");         // line 4
    for (int i = 0; i < 3; ++i) fixture.window.step();
    RV_CHECK_EQ(depth_of(fixture.editor(), 3), 0);  // fetched from straight ahead

    fixture.window.step();  // the beq reaches EX and the target is known
    RV_CHECK_EQ(depth_of(fixture.editor(), 3), -1);
    RV_CHECK_EQ(depth_of(fixture.editor(), 4), 0);  // the target, fetched instead
}

RV_TEST(gui, a_source_line_step_leaves_every_stage_filled) {
    // Driving with Step rather than Step Cycle used to leave MEM and WB empty.
    Fixture fixture;
    step_to_next_line(fixture.window);  // li a0, 1
    step_to_next_line(fixture.window);  // li a1, 0x12345 -- two instructions

    auto* panel = fixture.window.findChild<PipelineWidget*>();
    RV_CHECK(panel != nullptr);
    RV_CHECK(!has(panel->stage_summary(), "MEM=-"));
    RV_CHECK(!has(panel->stage_summary(), "WB=-"));
}

RV_TEST(gui, only_the_finishing_line_is_coloured_the_rest_are_named) {
    Fixture fixture;
    for (int i = 0; i < 5; ++i) fixture.window.step();

    // Line 4's instruction has reached the last stage, and that is the one line
    // with a colour; the others are marked in the margin by name.
    const PipelineMark* finishing = fixture.editor().pipeline_mark(4);
    RV_CHECK(finishing != nullptr);
    RV_CHECK_EQ(finishing->depth, 4);
    RV_CHECK(fixture.editor().pipeline_mark(5) != nullptr);
    RV_CHECK_NE(fixture.editor().pipeline_mark(5)->depth, 4);
}

RV_TEST(gui, the_branch_colours_turn_off_on_their_own) {
    Fixture fixture(
        "_start: li   a0, 0\n"
        "        beq  a0, x0, out\n"
        "        li   a1, 99\n"
        "out:    ebreak\n");
    for (int i = 0; i < 6; ++i) fixture.window.step();
    RV_CHECK(tone_of(fixture.editor(), 2) == PipelineTone::Taken);

    fixture.window.set_branch_colours(false);
    RV_CHECK(tone_of(fixture.editor(), 2) == PipelineTone::Pending);
    // The stage marking itself is untouched -- the two are independent.
    RV_CHECK_EQ(depth_of(fixture.editor(), 2), 4);
    RV_CHECK(fixture.window.pipeline_colours());

    fixture.window.set_branch_colours(true);
    RV_CHECK(tone_of(fixture.editor(), 2) == PipelineTone::Taken);
}
