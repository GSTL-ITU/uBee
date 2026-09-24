// rv32 -- a RISC-V RV32IMC_Zicsr_Zifencei emulator, assembler and debugger.
//
// The command handling here is deliberately thin. From M4 the real command set
// lives in rv_cmd, shared by the CLI and the TUI, and this file shrinks to
// argument parsing and dispatch.
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include <fstream>
#include <iostream>
#include <sstream>

#include "asm/assembler.hpp"
#include "cmd/repl.hpp"
#include "cmd/tty.hpp"
#include "asm/diag_render.hpp"
#include "core/hart.hpp"
#include "core/device_config.hpp"
#include "core/memfile.hpp"
#include "isa/disasm.hpp"
#include "isa/regnames.hpp"

namespace {

using namespace rv;

constexpr const char* kVersion = "rv32 " RV32_VERSION " (RV32IMC_Zicsr_Zifencei)";

void print_usage() {
    std::puts(
        "usage: rv32 <command> [options]\n"
        "\n"
        "commands:\n"
        "  asm <file.s>          assemble to .mem images\n"
        "  run <file.s|.mem>     assemble if needed, then run\n"
        "  dis <imem.mem>        disassemble a $readmemh image\n"
        "  exec <word>...        execute raw instruction words given as hex\n"
        "  test <file.s>...      run self-checking assembly tests\n"
        "  dbg <file.s>          open the interactive debugger\n"
        "  --version, --help\n"
        "\n"
        "The desktop interface is a separate binary:  rv32-gui <file.s>\n"
        "\n"
        "options:\n"
        "  -o <file>             instruction image to write (default imem.mem)\n"
        "  --dmem <file>         data image to write or load (default dmem.mem)\n"
        "  --pad                 emit the full memory array, not just what is used\n"
        "  --devices <file.toml> build the machine a board file describes: its\n"
        "                        peripherals, and the memory sizes --pad pads to\n"
        "                        (--board is the same option under its older name)\n"
        "  --annotate            add disassembly comments to the .mem output\n"
        "  --strict-word-mem     warn on sub-word loads and stores\n"
        "  --trace               print each instruction as it executes\n"
        "  --limit <n>           stop after n instructions (default 1000000)\n"
        "  --numeric             print x10 rather than a0\n"
        "  --quiet               only print the final state\n");
}

struct Options {
    std::string output_path;
    std::string dmem_path;
    u64 limit = 1'000'000;
    bool trace = false;
    bool quiet = false;
    bool numeric = false;
    bool pad = false;
    bool annotate = false;
    bool strict_word_mem = false;
    /// Emit the teaching notes (the 'la' sizing explanation). Useful while
    /// learning, pure noise in a test runner.
    bool explain = true;
    /// A machine description: the peripherals a run gets, and the memory sizes
    /// it gets them alongside. What --pad pads to has to be the size of the
    /// array the file will be loaded into, and that is a property of the
    /// machine rather than of this run.
    std::string machine;
    std::vector<std::string> positional;
};

/// Returns false and prints a message if an option is malformed.
bool parse_options(const std::vector<std::string>& args, Options& out) {
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--trace") {
            out.trace = true;
        } else if (arg == "--quiet") {
            out.quiet = true;
        } else if (arg == "--numeric") {
            out.numeric = true;
        } else if (arg == "--pad") {
            out.pad = true;
        } else if (arg == "--annotate") {
            out.annotate = true;
                } else if (arg == "--strict-word-mem") {
            out.strict_word_mem = true;
        } else if (arg == "--limit" || arg == "--dmem" || arg == "-o" || arg == "--devices" ||
                   arg == "--board") {
            if (i + 1 >= args.size()) {
                std::fprintf(stderr, "rv32: %s needs a value\n", arg.c_str());
                return false;
            }
            if (arg == "--limit") {
                out.limit = std::strtoull(args[++i].c_str(), nullptr, 0);
            } else if (arg == "-o") {
                out.output_path = args[++i];
            } else if (arg == "--devices" || arg == "--board") {
                // One file and one option. --board named it back when the only
                // thing read out of it was the pair of memory sizes; it names
                // the same file, so it keeps working rather than becoming an
                // error someone has to look up.
                out.machine = args[++i];
            } else {
                out.dmem_path = args[++i];
            }
        } else if (arg.rfind("-", 0) == 0 && arg != "-") {
            std::fprintf(stderr, "rv32: unknown option '%s'\n", arg.c_str());
            return false;
        } else {
            out.positional.push_back(arg);
        }
    }
    return true;
}

/// Show only the registers that are not zero. Printing all 32 buries the two or
/// three a small program actually touched.
void print_nonzero_registers(const core::CpuState& cpu, bool numeric) {
    std::printf("registers (non-zero only):\n");
    bool any = false;
    for (RegIdx r = 1; r < kNumRegs; ++r) {
        if (cpu.x[r] == 0) continue;
        any = true;
        if (numeric) {
            std::printf("  %-4s 0x%08x  %d\n", isa::numeric_name(r).data(), cpu.x[r],
                        static_cast<i32>(cpu.x[r]));
        } else {
            std::printf("  %-3s %-4s 0x%08x  %d\n", isa::numeric_name(r).data(),
                        isa::abi_name(r).data(), cpu.x[r], static_cast<i32>(cpu.x[r]));
        }
    }
    if (!any) std::printf("  (all zero)\n");
    std::printf("  pc       0x%08x   cycle %llu  instret %llu\n", cpu.pc,
                static_cast<unsigned long long>(cpu.cycle),
                static_cast<unsigned long long>(cpu.instret));
}

void print_trace_line(const core::StepOutcome& outcome, const core::Hart& hart, bool numeric) {
    isa::DisasmOptions disasm_options;
    disasm_options.abi_names = !numeric;
    const std::string text = isa::disassemble(outcome.instr, outcome.pc_before, disasm_options);

    // A compressed instruction is 16 bits, and padding it to eight digits would
    // suggest the fetch read four bytes when it read two. Right-aligned in the
    // same column so the listing still lines up.
    char bits[16];
    if (outcome.instr.length == 2) {
        std::snprintf(bits, sizeof bits, "    %04x", outcome.raw & 0xffffu);
    } else {
        std::snprintf(bits, sizeof bits, "%08x", outcome.raw);
    }
    std::printf("%08x  %s  %-26s", outcome.pc_before, bits, text.c_str());
    if (outcome.reg_written != core::kNoReg) {
        const RegIdx reg = outcome.reg_written;
        std::printf("%s = 0x%08x",
                    numeric ? isa::numeric_name(reg).data() : isa::abi_name(reg).data(),
                    hart.cpu().x[reg]);
    } else if (outcome.mem_written != core::kNoAddr) {
        std::printf("[0x%08x] <- %u bytes", outcome.mem_written, outcome.mem_width);
    }
    if (outcome.trapped) std::printf("  TRAP: %s", core::trap_cause_name(outcome.cause));
    std::printf("\n");
}

/// Run to completion and report why it stopped.
void run_machine(core::Hart& hart, const Options& options) {
    if (options.trace) {
        std::printf("addr      word      instruction               effect\n");
        std::printf("---------------------------------------------------------------\n");
    }

    u64 executed = 0;
    while (!hart.halted() && executed < options.limit) {
        const core::StepOutcome outcome = hart.step();
        ++executed;
        if (options.trace) print_trace_line(outcome, hart, options.numeric);
        if (outcome.halt != core::HaltReason::None) break;
    }

    if (hart.halted()) {
        std::printf("\nstopped: %s\n", core::halt_reason_name(hart.halt_reason()));
    } else {
        // Almost always an infinite loop. Say so rather than just stopping.
        std::printf("\nstopped: instruction limit (%llu) reached -- infinite loop?\n",
                    static_cast<unsigned long long>(options.limit));
    }

    if (!options.quiet) {
        std::printf("\n");
        print_nonzero_registers(hart.cpu(), options.numeric);
    }
    const std::string& uart = hart.bus().uart_output();
    if (!uart.empty()) std::printf("\nuart output:\n%s\n", uart.c_str());
}

/// Read the machine description, if one was named. Not naming one is not an
/// error: it means the machine a hart has by default.
bool read_machine(const Options& options, core::DeviceConfig& out) {
    if (options.machine.empty()) return true;
    out = core::read_device_config(options.machine);
    if (!out.ok()) {
        std::fprintf(stderr, "rv32: %s\n", out.error.c_str());
        return false;
    }
    return true;
}

/// Give a hart the peripherals and the memory sizes a description already read
/// names. `report_warnings` is false for the second and later machines built
/// from one file, whose overlaps would be the same sentence again.
///
/// Every caller runs this *before* loading the program image, because applying
/// a configuration can resize memory and a resize truncates what no longer
/// fits -- configuring afterwards would quietly cut the program down.
bool apply_machine(core::Hart& hart, const core::DeviceConfig& config, const Options& options,
                   bool report_warnings = true) {
    if (options.machine.empty()) return true;

    std::vector<std::string> warnings;
    const std::string error = core::apply_machine_config(
        hart, config, core::directory_of(options.machine), &warnings);
    if (!error.empty()) {
        std::fprintf(stderr, "rv32: %s\n", error.c_str());
        return false;
    }
    // Overlapping addresses are something the format deliberately allows, so
    // this is a warning rather than a refusal -- but never a silent one: a
    // device that quietly stopped answering is the bug that costs an afternoon.
    if (report_warnings) {
        for (const std::string& warning : warnings) {
            std::fprintf(stderr, "rv32: warning: %s\n", warning.c_str());
        }
    }
    return true;
}

/// Read and apply, for the commands that build one machine and assemble
/// nothing into it.
bool configure_machine(core::Hart& hart, const Options& options) {
    core::DeviceConfig config;
    return read_machine(options, config) && apply_machine(hart, config, options);
}

/// The assembler needs the memory sizes too, and they have to be the ones the
/// program will actually run on: loading truncates at the end of the memory, so
/// a program too big for the machine described in the file becomes a different
/// program unless the assembler is holding the same numbers and can say so.
as::AssembleOptions assemble_options_for(const Options& options,
                                         const core::DeviceConfig& machine) {
    as::AssembleOptions out;
    out.strict_word_mem = options.strict_word_mem;
    out.explain_pseudo_sizing = options.explain;
    if (machine.imem_size != 0) out.imem_size = machine.imem_size;
    if (machine.dmem_size != 0) out.dmem_size = machine.dmem_size;
    if (machine.has_imem_base) out.imem_base = machine.imem_base;
    if (machine.has_dmem_base) out.dmem_base = machine.dmem_base;
    if (machine.has_reset) out.reset_entry = machine.reset;
    return out;
}

bool ends_with(const std::string& text, const std::string& suffix) {
    return text.size() >= suffix.size() &&
           text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

/// Assemble a file, printing diagnostics. Returns false if it did not assemble.
bool assemble_file(const std::string& path, const Options& options,
                   const core::DeviceConfig& machine, as::AssembledProgram& out) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        std::fprintf(stderr, "rv32: cannot open '%s'\n", path.c_str());
        return false;
    }
    std::ostringstream contents;
    contents << file.rdbuf();

    const as::SourceFile source(path, contents.str());
    out = as::assemble(source, assemble_options_for(options, machine));

    if (!out.diagnostics.empty()) {
        as::RenderOptions render_options;
        // Colour only when stdout is a terminal, so redirecting to a file gives
        // clean text.
        render_options.color = cmd::supports_color(stderr);
        std::fputs(as::render_all(out.diagnostics, source, render_options).c_str(), stderr);
    }
    return out.ok();
}

int command_asm(const std::vector<std::string>& args) {
    Options options;
    if (!parse_options(args, options)) return 2;
    if (options.positional.size() != 1) {
        std::fprintf(stderr, "rv32: asm needs exactly one source file\n");
        return 2;
    }

    core::DeviceConfig machine;
    if (!read_machine(options, machine)) return 1;

    as::AssembledProgram program;
    if (!assemble_file(options.positional[0], options, machine, program)) return 1;

    const std::string imem_path = options.output_path.empty() ? "imem.mem" : options.output_path;
    const std::string dmem_path = options.dmem_path.empty() ? "dmem.mem" : options.dmem_path;

    // The depths to pad to. A board file names them; without one they are what
    // a machine has by default.
    u32 imem_bytes = core::kDefaultImemSize;
    u32 dmem_bytes = core::kDefaultDmemSize;
    if (machine.imem_size != 0) imem_bytes = machine.imem_size;
    if (machine.dmem_size != 0) dmem_bytes = machine.dmem_size;

    core::MemFileOptions mem_options;
    mem_options.pad_to_full = options.pad;
    mem_options.annotate = options.annotate;

    mem_options.full_size = imem_bytes / 4;
    if (const core::MemFileResult result =
            core::write_mem_file(imem_path, program.imem_words, mem_options);
        !result.ok) {
        std::fprintf(stderr, "rv32: %s\n", result.error.c_str());
        return 1;
    }

    const std::vector<Word> dmem_words = core::pack_bytes_to_words(program.dmem_bytes);
    mem_options.full_size = dmem_bytes / 4;
    mem_options.annotate = false;  // disassembling data would be nonsense
    if (const core::MemFileResult result = core::write_mem_file(dmem_path, dmem_words, mem_options);
        !result.ok) {
        std::fprintf(stderr, "rv32: %s\n", result.error.c_str());
        return 1;
    }

    if (!options.quiet) {
        // Instructions, not words: with the C extension a word can hold two.
        std::printf("%s: %zu instructions (%zu bytes)\n", imem_path.c_str(),
                    program.map.entries().size(), program.imem_bytes);
        std::printf("%s: %zu bytes of data\n", dmem_path.c_str(), program.dmem_bytes.size());
    }
    return 0;
}

/// Run self-checking assembly programs, riscv-tests style.
///
/// The convention: the program computes, leaves 0 in a0 on success or the
/// number of the check that failed, and stops at ebreak. These are the highest
/// value tests in the project -- one program exercises the assembler, the
/// encoder, the decoder, the executor and the traps at once, and it is written
/// in the language the tool exists for.
int command_test(const std::vector<std::string>& args) {
    Options options;
    options.explain = false;
    if (!parse_options(args, options)) return 2;
    if (options.positional.empty()) {
        std::fprintf(stderr, "rv32: test needs at least one .s file\n");
        return 2;
    }

    // Read before assembling anything: a machine file that will not parse is
    // worth saying once, up front, rather than after thirty programs have been
    // assembled into a machine that was never built.
    core::DeviceConfig machine;
    if (!read_machine(options, machine)) return 2;

    int failures = 0;
    bool warned = false;
    for (const std::string& path : options.positional) {
        as::AssembledProgram program;
        if (!assemble_file(path, options, machine, program)) {
            std::printf("FAIL  %s (did not assemble)\n", path.c_str());
            ++failures;
            continue;
        }

        core::Hart hart;
        if (!apply_machine(hart, machine, options, !warned)) return 2;
        warned = true;
        hart.imem().load_words(program.imem_words);
        hart.reset(program.entry);
        hart.bus().dmem().load_bytes(program.dmem_bytes);

        u64 executed = 0;
        const u64 budget = options.limit;
        while (!hart.halted() && executed < budget) {
            hart.step();
            ++executed;
        }

        if (!hart.halted()) {
            std::printf("FAIL  %s (no ebreak after %llu instructions -- infinite loop?)\n",
                        path.c_str(), static_cast<unsigned long long>(budget));
            ++failures;
            continue;
        }
        if (hart.halt_reason() != core::HaltReason::Ebreak) {
            std::printf("FAIL  %s (%s at pc=0x%08x)\n", path.c_str(),
                        core::halt_reason_name(hart.halt_reason()), hart.cpu().pc);
            ++failures;
            continue;
        }

        const u32 result = hart.cpu().x[10];  // a0
        if (result != 0) {
            std::printf("FAIL  %s (check #%u failed)\n", path.c_str(), result);
            ++failures;
            continue;
        }
        std::printf(" ok   %s (%llu instructions)\n", path.c_str(),
                    static_cast<unsigned long long>(executed));
    }

    if (failures > 0) std::printf("\n%d of %zu failed\n", failures, options.positional.size());
    return failures == 0 ? 0 : 1;
}

int command_dbg(const std::vector<std::string>& args) {
    Options options;
    if (!parse_options(args, options)) return 2;
    if (options.positional.size() != 1) {
        std::fprintf(stderr, "rv32: dbg needs exactly one source file\n");
        return 2;
    }

    std::ifstream file(options.positional[0], std::ios::binary);
    if (!file) {
        std::fprintf(stderr, "rv32: cannot open '%s'\n", options.positional[0].c_str());
        return 1;
    }
    std::ostringstream contents;
    contents << file.rdbuf();

    core::DeviceConfig machine;
    if (!read_machine(options, machine)) return 1;

    dbg::DebugSession session;
    if (!apply_machine(session.hart(), machine, options)) return 1;

    if (!session.load_source(contents.str(), options.positional[0],
                             assemble_options_for(options, machine))) {
        as::RenderOptions render_options;
        render_options.color = cmd::supports_color(stderr);
        std::fputs(as::render_all(session.program().diagnostics, session.source(), render_options)
                       .c_str(),
                   stderr);
        return 1;
    }

    const bool interactive = cmd::is_terminal(stdin);
    cmd::ReplOptions repl_options;
    repl_options.color = cmd::supports_color(stdout);
    // A piped script's output is unreadable without knowing which command
    // produced it, so echo when we are not talking to a person.
    repl_options.echo = !interactive;
    repl_options.format.numeric_registers = options.numeric;

    std::printf("%s\n%zu instructions, entry 0x%08x. Type 'help' for commands.\n\n", kVersion,
                session.program().map.entries().size(), session.program().entry);
    std::fputs(cmd::format_location(session, repl_options.format).c_str(), stdout);

    cmd::Repl repl(session, repl_options);
    return repl.run(std::cin, std::cout);
}

int command_exec(const std::vector<std::string>& args) {
    Options options;
    options.limit = 1000;
    options.trace = true;
    if (!parse_options(args, options)) return 2;

    // Arguments are laid out as a byte stream so that compressed and full-width
    // instructions can be mixed. The digit count decides the width: four hex
    // digits is a halfword, eight is a word. `rv32 exec 4505 00150513` is a
    // c.li followed by an addi.
    std::vector<u8> bytes;
    for (const std::string& text : options.positional) {
        const std::string digits =
            text.compare(0, 2, "0x") == 0 || text.compare(0, 2, "0X") == 0 ? text.substr(2) : text;
        char* end = nullptr;
        const unsigned long long value = std::strtoull(digits.c_str(), &end, 16);
        if (digits.empty() || end == digits.c_str() || *end != '\0' || value > 0xffff'ffffull) {
            std::fprintf(stderr, "rv32: '%s' is not a hex instruction word\n", text.c_str());
            return 2;
        }
        const bool half = digits.size() <= 4;
        if (half && (value & 3u) == 3u) {
            std::fprintf(stderr,
                         "rv32: '%s' is four digits, so it is read as a 16-bit instruction, "
                         "but its low two bits say it is 32-bit; write all eight digits\n",
                         text.c_str());
            return 2;
        }
        const int width = half ? 2 : 4;
        for (int i = 0; i < width; ++i) bytes.push_back(static_cast<u8>(value >> (8 * i)));
    }
    const std::vector<Word> words = core::pack_bytes_to_words(bytes);
    if (words.empty()) {
        std::fprintf(stderr, "rv32: no instruction words given\n");
        return 2;
    }

    core::Hart hart;
    if (!configure_machine(hart, options)) return 1;
    hart.imem().load_words(words);
    hart.reset(0);
    run_machine(hart, options);
    return 0;
}

int command_run(const std::vector<std::string>& args) {
    Options options;
    if (!parse_options(args, options)) return 2;
    if (options.positional.size() != 1) {
        std::fprintf(stderr, "rv32: run needs exactly one file\n");
        return 2;
    }

    const std::string& path = options.positional[0];

    core::DeviceConfig machine;
    if (!read_machine(options, machine)) return 1;

    // Assembly source runs directly: there is no reason to make someone write
    // the .mem file out first just to run their own program.
    if (ends_with(path, ".s") || ends_with(path, ".S") || ends_with(path, ".asm")) {
        core::Hart hart;
        if (!apply_machine(hart, machine, options)) return 1;

        as::AssembledProgram program;
        if (!assemble_file(path, options, machine, program)) return 1;

        hart.imem().load_words(program.imem_words);
        hart.reset(program.entry);
        hart.bus().dmem().load_bytes(program.dmem_bytes);
        run_machine(hart, options);
        return 0;
    }

    std::vector<Word> imem_words;
    if (const core::MemFileResult result = core::read_mem_file(path, imem_words); !result.ok) {
        std::fprintf(stderr, "rv32: %s\n", result.error.c_str());
        return 1;
    }

    core::Hart hart;
    if (!apply_machine(hart, machine, options)) return 1;
    hart.imem().load_words(imem_words);

    if (!options.dmem_path.empty()) {
        std::vector<Word> dmem_words;
        if (const core::MemFileResult result = core::read_mem_file(options.dmem_path, dmem_words);
            !result.ok) {
            std::fprintf(stderr, "rv32: %s\n", result.error.c_str());
            return 1;
        }
        hart.reset(0);
        hart.bus().dmem().load_words(dmem_words);
    } else {
        hart.reset(0);
    }

    run_machine(hart, options);
    return 0;
}

int command_dis(const std::vector<std::string>& args) {
    Options options;
    if (!parse_options(args, options)) return 2;
    if (options.positional.size() != 1) {
        std::fprintf(stderr, "rv32: dis needs exactly one image file\n");
        return 2;
    }

    std::vector<Word> words;
    if (const core::MemFileResult result = core::read_mem_file(options.positional[0], words);
        !result.ok) {
        std::fprintf(stderr, "rv32: %s\n", result.error.c_str());
        return 1;
    }

    isa::DisasmOptions disasm_options;
    disasm_options.abi_names = !options.numeric;

    // The image is words, but the instructions in it are a halfword stream: a
    // word can hold two compressed instructions, and a 32-bit one can straddle
    // the boundary between two words. So walk halfwords and let each
    // instruction say how far to advance.
    const auto half_at = [&](std::size_t index) -> u32 {
        const std::size_t word = index / 2;
        if (word >= words.size()) return 0;
        return (index % 2 == 0) ? (words[word] & 0xffffu) : (words[word] >> 16);
    };

    const std::size_t half_count = words.size() * 2;
    for (std::size_t i = 0; i < half_count;) {
        const Addr address = static_cast<Addr>(i * 2);
        const u32 low = half_at(i);
        const bool wide = isa::instruction_length(low) == 4 && i + 1 < half_count;
        const Word word = wide ? (low | (half_at(i + 1) << 16)) : low;

        if (wide) {
            std::printf("%08x:  %08x  %s\n", address, word,
                        isa::disassemble_word(word, address, disasm_options).c_str());
        } else {
            std::printf("%08x:      %04x  %s\n", address, word & 0xffffu,
                        isa::disassemble_word(word, address, disasm_options).c_str());
        }
        i += wide ? 2 : 1;
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    const std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty() || args[0] == "--help" || args[0] == "-h") {
        print_usage();
        return args.empty() ? 2 : 0;
    }
    if (args[0] == "--version") {
        std::puts(kVersion);
        return 0;
    }

    const std::vector<std::string> rest(args.begin() + 1, args.end());
    if (args[0] == "asm") return command_asm(rest);
    if (args[0] == "test") return command_test(rest);
    if (args[0] == "dbg") return command_dbg(rest);
    if (args[0] == "exec") return command_exec(rest);
    if (args[0] == "run") return command_run(rest);
    if (args[0] == "dis") return command_dis(rest);

    std::fprintf(stderr, "rv32: unknown command '%s'\n", args[0].c_str());
    print_usage();
    return 2;
}
