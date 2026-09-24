cmake_minimum_required(VERSION 3.20)
project(rv32_emulator VERSION 1.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(RV32_WERROR "Treat all warnings as errors" OFF)
option(RV32_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
include(CompilerWarnings)

# ---------------------------------------------------------------------------
# rv_isa -- the instruction table, encoding and decoding. Depends on nothing.
# ---------------------------------------------------------------------------
add_library(rv_isa STATIC
    src/isa/isa.cpp
    src/isa/csr.cpp
    src/isa/decode.cpp
    src/isa/expand.cpp
    src/isa/encode.cpp
    src/isa/disasm.cpp
    src/isa/regnames.cpp
)
target_include_directories(rv_isa PUBLIC src)
rv_set_warnings(rv_isa)

# ---------------------------------------------------------------------------
# rv_core -- the machine: registers, memory, MMIO, execution, traps.
# ---------------------------------------------------------------------------
add_library(rv_core STATIC
    src/core/csrfile.cpp
    src/core/memory.cpp
    src/core/devices.cpp
    src/core/device_config.cpp
    src/core/bus.cpp
    src/core/exec.cpp
    src/core/hart.cpp
    src/core/memfile.cpp
    src/core/trap.cpp
)
target_link_libraries(rv_core PUBLIC rv_isa)
rv_set_warnings(rv_core)

# ---------------------------------------------------------------------------
# rv_asm -- lexer, parser, assembler. Depends on rv_core because it produces
# images for a specific machine: it needs the memory sizes to report "program
# too large", and it emits the same .mem format the runner consumes.
# ---------------------------------------------------------------------------
add_library(rv_asm STATIC
    src/asm/source.cpp
    src/asm/diagnostic.cpp
    src/asm/diag_render.cpp
    src/asm/directive.cpp
    src/asm/lexer.cpp
    src/asm/complete.cpp
    src/asm/symtab.cpp
    src/asm/pseudo.cpp
    src/asm/parser.cpp
    src/asm/sourcemap.cpp
    src/asm/assembler.cpp
)
target_link_libraries(rv_asm PUBLIC rv_core)
rv_set_warnings(rv_asm)

# ---------------------------------------------------------------------------
# rv_dbg -- the debugger engine: breakpoints, stepping, reverse step. The one
# facade the CLI, the TUI and the tests all talk to.
# ---------------------------------------------------------------------------
add_library(rv_dbg STATIC
    src/dbg/history.cpp
    src/dbg/breakpoints.cpp
    src/dbg/session.cpp
    src/dbg/pipeline.cpp
)
target_link_libraries(rv_dbg PUBLIC rv_asm)
rv_set_warnings(rv_dbg)

# ---------------------------------------------------------------------------
# rv_cmd -- the command set and the line-based REPL, shared by both front-ends.
# Must not depend on any UI toolkit: the window and the command line both
# reach this same registry, so neither can grow its own idea of what `step`
# means.
# ---------------------------------------------------------------------------
add_library(rv_cmd STATIC
    src/cmd/format.cpp
    src/cmd/commands.cpp
    src/cmd/repl.cpp
    src/cmd/tty.cpp
)
target_link_libraries(rv_cmd PUBLIC rv_dbg)
rv_set_warnings(rv_cmd)
# The version is declared once, in project() above, and reaches both
# front-ends through the library they share -- so a tag cannot say one
# thing and `--version` another.
target_compile_definitions(rv_cmd PUBLIC RV32_VERSION="${PROJECT_VERSION}")

# ---------------------------------------------------------------------------
# rv_gui -- the desktop interface.
#
# Optional: without Qt everything else still builds, so CI and anyone who only
# wants the CLI are unaffected. Like the TUI, it is a shell over DebugSession
# and rv_cmd rather than a second implementation of anything.
# ---------------------------------------------------------------------------
find_package(Qt6 QUIET COMPONENTS Widgets)

if(Qt6_FOUND)
    set(CMAKE_AUTOMOC ON)
    set(CMAKE_AUTORCC ON)
    add_executable(rv32-gui
        packaging/rv32.qrc
        src/gui/gui_main.cpp
        src/gui/main_window.cpp
        src/gui/device_panel.cpp
        src/gui/editor_widget.cpp
        src/gui/encoding_widget.cpp
        src/gui/pipeline_widget.cpp
        src/gui/highlighter.cpp
    )
    target_link_libraries(rv32-gui PRIVATE rv_cmd Qt6::Widgets)
    rv_set_warnings(rv32-gui)
    # Qt's generated moc code trips warnings we hold our own code to.
    target_compile_options(rv32-gui PRIVATE -Wno-old-style-cast)
    # The GUI is tested the same way the TUI is: created for real, driven
    # through the actions the toolbar calls, then asked what it shows. Qt's
    # offscreen platform means no display is needed, so this runs in CI.
    add_executable(rv_gui_tests
        tests/gui_test.cpp
        src/gui/main_window.cpp
        src/gui/device_panel.cpp
        src/gui/editor_widget.cpp
        src/gui/encoding_widget.cpp
        src/gui/pipeline_widget.cpp
        src/gui/highlighter.cpp
    )
    target_link_libraries(rv_gui_tests PRIVATE rv_cmd Qt6::Widgets)
    target_include_directories(rv_gui_tests PRIVATE tests)
    rv_set_warnings(rv_gui_tests)
    target_compile_options(rv_gui_tests PRIVATE -Wno-old-style-cast)

    if(WIN32)
        # Without this a console window opens behind the app every time it is
        # started from Explorer. windeployqt supplies the Qt DLLs beside the
        # exe; the compiler's own runtime is linked in so it need not.
        set_target_properties(rv32-gui PROPERTIES WIN32_EXECUTABLE ON)
        target_link_options(rv32-gui PRIVATE -static-libgcc -static-libstdc++)
    endif()

    message(STATUS "Qt6 found: building rv32-gui")
else()
    message(STATUS "Qt6 not found: skipping rv32-gui (the CLI and TUI are unaffected)")
endif()

# ---------------------------------------------------------------------------
# rv32 -- the front-end binary.
# ---------------------------------------------------------------------------
add_executable(rv32 src/main.cpp)
target_link_libraries(rv32 PRIVATE rv_cmd)
rv_set_warnings(rv32)

if(WIN32)
    # The CLI depends on nothing but the C++ runtime, so it may as well be a
    # single file you can copy onto a lab machine and run.
    target_link_options(rv32 PRIVATE -static)
endif()

# ---------------------------------------------------------------------------
# Tests. No external framework: tests/rv_test.hpp is the whole harness.
# ---------------------------------------------------------------------------
enable_testing()

add_executable(rv_tests
    tests/test_main.cpp
    tests/unit_fields.cpp
    tests/unit_decode.cpp
    tests/unit_expand.cpp
    tests/unit_roundtrip.cpp
    tests/unit_exec_alu.cpp
    tests/unit_exec_mem.cpp
    tests/unit_exec_branch.cpp
    tests/unit_exec_m.cpp
    tests/unit_exec_coverage.cpp
    tests/unit_csr_trap.cpp
    tests/unit_interrupt.cpp
    tests/unit_memfile.cpp
    tests/unit_devices.cpp
    tests/unit_lexer.cpp
    tests/unit_diag_render.cpp
    tests/unit_assembler.cpp
    tests/unit_complete.cpp
    tests/unit_session.cpp
    tests/unit_pipeline.cpp
)
target_link_libraries(rv_tests PRIVATE rv_cmd)
target_include_directories(rv_tests PRIVATE tests)
rv_set_warnings(rv_tests)

# One CTest entry per suite, so a failure can be re-run in isolation.
foreach(suite fields decode expand roundtrip exec_alu exec_mem exec_branch exec_m exec_coverage csr_trap interrupt memfile devices
              lexer diag_render assembler complete session pipeline)
    add_test(NAME unit.${suite} COMMAND rv_tests --filter=${suite})
endforeach()

if(TARGET rv_gui_tests)
    add_test(NAME unit.gui COMMAND rv_gui_tests)
    set_tests_properties(unit.gui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

# Self-checking assembly programs: one CTest each, so a failure names the file.
# Globbed at configure time, so re-run cmake after adding one.
file(GLOB RV32_ASM_TESTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/asm/*.s)
foreach(asm_test ${RV32_ASM_TESTS})
    get_filename_component(asm_name ${asm_test} NAME_WE)
    add_test(NAME asm.${asm_name} COMMAND rv32 test ${asm_test})
endforeach()

# The same thing on a machine described by a file. Kept out of the glob above
# because it is the one program that needs an option: it is what covers
# --devices end to end, and it fails on the default machine by design -- the
# peripheral it drives and the memory it stores into both come from the file.
add_test(NAME board.multiplier
         COMMAND rv32 test ${CMAKE_CURRENT_SOURCE_DIR}/tests/board/multiplier.s
                 --devices ${CMAKE_CURRENT_SOURCE_DIR}/tests/board/multiplier.toml)

# The other half of it. Loading truncates at the end of the memory, so a
# program too big for the machine in the file has to be reported by the
# assembler rather than silently cut down into a different program -- which
# means the sizes have to reach the assembler and not only the run.
add_test(NAME board.too_large
         COMMAND rv32 asm ${CMAKE_CURRENT_SOURCE_DIR}/tests/board/multiplier.s
                 --devices ${CMAKE_CURRENT_SOURCE_DIR}/tests/board/too_small.toml)
set_tests_properties(board.too_large PROPERTIES
                     PASS_REGULAR_EXPRESSION "does not fit in instruction memory")
