// The GUI entry point.
//
// Everything of substance is below this: DebugSession runs the machine, rv_asm
// assembles, rv_cmd holds the command set. This is a shell around them.
#include <QApplication>
#include <QCommandLineParser>
#include <QIcon>
#include <QPalette>
#include <QTextCursor>
#include <QStyleFactory>

#include "gui/editor_widget.hpp"
#include "gui/main_window.hpp"

namespace {

/// The editor and the encoding panel are dark by design -- code is easier to
/// read that way and the colour roles were chosen for it. Without this the rest
/// of the window stays whatever the desktop theme is, and the two halves fight.
void apply_dark_theme() {
    QApplication::setStyle(QStyleFactory::create("Fusion"));

    const QColor window(0x24, 0x27, 0x2e);
    const QColor base(0x1e, 0x21, 0x27);
    const QColor alternate(0x28, 0x2c, 0x34);
    const QColor text(0xd6, 0xd6, 0xd6);
    const QColor bright(0xf0, 0xf0, 0xf0);
    const QColor highlight(0x3d, 0x59, 0x7e);
    const QColor disabled(0x6a, 0x71, 0x7c);

    QPalette palette;
    palette.setColor(QPalette::Window, window);
    palette.setColor(QPalette::WindowText, text);
    palette.setColor(QPalette::Base, base);
    palette.setColor(QPalette::AlternateBase, alternate);
    palette.setColor(QPalette::ToolTipBase, window);
    palette.setColor(QPalette::ToolTipText, text);
    palette.setColor(QPalette::Text, text);
    palette.setColor(QPalette::Button, window);
    palette.setColor(QPalette::ButtonText, text);
    palette.setColor(QPalette::BrightText, bright);
    palette.setColor(QPalette::Highlight, highlight);
    palette.setColor(QPalette::HighlightedText, bright);
    palette.setColor(QPalette::Disabled, QPalette::Text, disabled);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, disabled);
    QApplication::setPalette(palette);
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("rv32");
    // The sizes are compiled in, so this works before anything is installed.
    // setDesktopFileName is what lets a desktop match the window back to the
    // .desktop entry -- without it the shell shows a generic icon in the dock
    // however well the theme is populated.
    QIcon icon;
    for (const int size : {16, 24, 32, 48, 64, 128, 256}) {
        icon.addFile(QString(":/rv32-%1.png").arg(size));
    }
    QApplication::setWindowIcon(icon);
    QGuiApplication::setDesktopFileName("rv32");
    apply_dark_theme();
    QApplication::setApplicationVersion(RV32_VERSION);

    QCommandLineParser parser;
    parser.setApplicationDescription("RV32IMC_Zicsr_Zifencei emulator, assembler and debugger");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument("file", "assembly file to open", "[file.s]");

    // The counterpart of the terminal front-end's --render-once: draw the
    // window to a file and exit. Useful for documentation, and for checking
    // the layout without a display.
    QCommandLineOption screenshot("screenshot", "render one frame to <file> and exit", "file");
    QCommandLineOption steps("steps", "advance this many pipeline cycles before rendering", "n",
                             "0");
    QCommandLineOption tab("tab", "select a dock tab before rendering", "name");
    QCommandLineOption caret("caret", "put the caret at <line>:<col> before rendering", "pos");
    parser.addOption(screenshot);
    parser.addOption(steps);
    parser.addOption(tab);
    parser.addOption(caret);
    QCommandLineOption suggest("suggest", "open the suggestion bar at the caret");
    parser.addOption(suggest);
    // A machine to build before the program is opened, so an exercise can ship
    // its board alongside its code.
    QCommandLineOption board("board", "build the machine described in <file.toml>", "file");
    parser.addOption(board);
    QCommandLineOption select("select-device", "select this row of the device list", "row");
    parser.addOption(select);
    parser.process(app);

    rv::gui::MainWindow window;
    if (parser.isSet(board)) window.load_devices(parser.value(board));
    const QStringList files = parser.positionalArguments();
    if (!files.isEmpty()) {
        window.open_file(files.first());
    } else {
        window.editor().setPlainText(
            "# Write assembly here, then press F5 to assemble and F8 to step.\n"
            "\n"
            "        .text\n"
            "        .globl _start\n"
            "_start:\n"
            "        li      a0, 5\n"
            "        li      a1, 3\n"
            "        add     a2, a0, a1\n"
            "        ebreak\n");
        window.assemble();
    }

    window.show();

    if (parser.isSet(screenshot)) {
        for (int i = 0, n = parser.value(steps).toInt(); i < n; ++i) window.step();
        if (parser.isSet(tab)) window.show_tab(parser.value(tab));
        if (parser.isSet(caret)) {
            const QStringList parts = parser.value(caret).split(':');
            window.editor().go_to_line(parts.value(0).toInt());
            QTextCursor cursor = window.editor().textCursor();
            cursor.movePosition(QTextCursor::StartOfBlock);
            cursor.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor,
                                parts.value(1, "0").toInt());
            window.editor().setTextCursor(cursor);
            window.editor().setFocus();
            if (parser.isSet(suggest)) window.editor().request_completion();
        }
        if (parser.isSet(select)) window.select_device(parser.value(select).toInt());
        QCoreApplication::processEvents();
        return window.grab().save(parser.value(screenshot)) ? 0 : 1;
    }
    return QApplication::exec();
}
