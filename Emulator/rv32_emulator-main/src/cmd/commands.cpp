#include "cmd/command.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <sstream>

#include "asm/diag_render.hpp"
#include "isa/regnames.hpp"

namespace rv::cmd {
namespace {

using dbg::StepKind;
using dbg::StopReason;

// ---------------------------------------------------------------------------
// Argument parsing
// ---------------------------------------------------------------------------

/// Resolve anything that can stand for a number: a literal, a register, a
/// symbol, or `$pc`. One function so every command accepts the same things --
/// `break main`, `x/16xw msg` and `print sp` all go through here.
std::optional<i64> resolve_value(const dbg::DebugSession& session, const std::string& text) {
    if (text.empty()) return std::nullopt;

    if (text == "$pc" || text == "pc") return session.cpu().pc;
    if (text == "$cycle") return static_cast<i64>(session.cpu().cycle);
    if (text == "$instret") return static_cast<i64>(session.cpu().instret);

    std::string name = text;
    if (name[0] == '$') name.erase(0, 1);
    if (const auto reg = isa::parse_register(name)) return session.cpu().x[*reg];
    if (const auto addr = session.lookup_symbol(name)) return *addr;

    char* end = nullptr;
    const long long value = std::strtoll(text.c_str(), &end, 0);
    if (end != text.c_str() && *end == '\0') return value;
    return std::nullopt;
}

std::optional<Addr> resolve_address(const dbg::DebugSession& session, const std::string& text) {
    if (const auto value = resolve_value(session, text)) return static_cast<Addr>(*value);
    return std::nullopt;
}

/// Explain a failed lookup, suggesting a symbol or register when one is close.
/// Same reasoning as the assembler's diagnostics: a name that is nearly right
/// is the common case, and pointing at the right one turns a dead end into a
/// fix.
std::string cannot_resolve(const dbg::DebugSession& session, const std::string& text) {
    std::vector<std::string_view> candidates = session.symbols().names();
    for (RegIdx reg = 0; reg < kNumRegs; ++reg) {
        candidates.push_back(isa::abi_name(reg));
        candidates.push_back(isa::numeric_name(reg));
    }
    std::string message = "cannot resolve '" + text + "'";
    if (const auto suggestion = as::closest_match(text, candidates); !suggestion.empty()) {
        message += " (did you mean '" + std::string(suggestion) + "'?)";
    } else if (session.symbols().all().empty()) {
        message += "; no symbols are loaded";
    } else {
        message += "; try 'info symbols' for what is defined";
    }
    return message;
}

/// Report where and why execution stopped, then show the new location.
void report(CmdContext& context, const dbg::StopEvent& event) {
    const std::string message = format_stop(context.session, event);
    if (!message.empty()) context.print(message);
    if (event.reason != StopReason::NotRunning) {
        context.print(format_location(context.session, context.format));
    }
}

CmdResult do_step(CmdContext& context, StepKind kind) {
    if (!context.session.has_program()) return CmdResult::failure("no program loaded");
    report(context, context.session.step(kind));
    return CmdResult::success();
}

CmdResult do_reverse(CmdContext& context, StepKind kind) {
    if (!context.session.can_reverse()) {
        return CmdResult::failure("no recorded history to step back through");
    }
    report(context, context.session.reverse_step(kind));
    return CmdResult::success();
}

// ---------------------------------------------------------------------------
// The `x` command's format spec: x/16xw addr
// ---------------------------------------------------------------------------

struct ExamineSpec {
    u32 count = 4;
    char format = 'x';  // x hex, d signed, u unsigned, i instruction, c characters
    char unit = 'w';    // b byte, h halfword, w word
};

ExamineSpec parse_examine_spec(const std::string& text) {
    ExamineSpec spec;
    std::size_t position = 0;
    std::string digits;
    while (position < text.size() && std::isdigit(static_cast<unsigned char>(text[position])) != 0) {
        digits.push_back(text[position++]);
    }
    if (!digits.empty()) spec.count = static_cast<u32>(std::strtoul(digits.c_str(), nullptr, 10));
    for (; position < text.size(); ++position) {
        const char c = text[position];
        if (c == 'b' || c == 'h' || c == 'w') {
            spec.unit = c;
        } else {
            spec.format = c;
        }
    }
    return spec;
}

std::string examine(const dbg::DebugSession& session, Addr addr, const ExamineSpec& spec,
                    const FormatOptions& options) {
    if (spec.format == 'i') return format_disassembly(session, addr & ~3u, spec.count, options);
    if (spec.format == 'x' && spec.unit == 'w') return format_memory(session, addr, spec.count, options);

    const u32 unit_size = spec.unit == 'b' ? 1u : (spec.unit == 'h' ? 2u : 4u);
    const u32 per_row = spec.unit == 'b' ? 8u : (spec.unit == 'h' ? 8u : 4u);
    std::ostringstream out;

    for (u32 index = 0; index < spec.count; ++index) {
        if (index % per_row == 0) {
            char prefix[24];
            std::snprintf(prefix, sizeof prefix, "%08x: ", addr + index * unit_size);
            out << (index == 0 ? "" : "\n") << prefix;
        }
        const Addr at = addr + index * unit_size;
        const u32 word = session.read_dmem_word(at & ~3u);
        const u32 shift = 8 * (at & 3u);
        u32 value = word >> shift;
        if (unit_size == 1) value &= 0xffu;
        if (unit_size == 2) value &= 0xffffu;

        char cell[32];
        switch (spec.format) {
            case 'd':
                std::snprintf(cell, sizeof cell, "%12d ",
                              sign_extend(value, static_cast<int>(unit_size) * 8));
                break;
            case 'u': std::snprintf(cell, sizeof cell, "%12u ", value); break;
            case 'c': {
                const auto character = static_cast<char>(value & 0xffu);
                std::snprintf(cell, sizeof cell, "%3d '%c' ", value,
                              character >= 0x20 && character < 0x7f ? character : '.');
                break;
            }
            default:
                std::snprintf(cell, sizeof cell, "%0*x ", static_cast<int>(unit_size) * 2, value);
                break;
        }
        out << cell;
    }
    out << "\n";
    return out.str();
}

// ---------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------

CmdResult cmd_help(CmdContext& context, const std::vector<std::string>& args) {
    const CommandRegistry& registry = CommandRegistry::instance();

    if (args.size() > 1) {
        const Command* command = registry.find(args[1]);
        if (command == nullptr) return CmdResult::failure("no such command: " + args[1]);
        std::ostringstream out;
        out << command->usage << "\n    " << command->help << "\n";
        if (!command->aliases.empty()) {
            out << "    aliases:";
            for (const std::string_view alias : command->aliases) out << " " << alias;
            out << "\n";
        }
        context.print(out.str());
        return CmdResult::success();
    }

    std::ostringstream out;
    out << "commands (type 'help <command>' for detail):\n";
    for (const Command& command : registry.all()) {
        char line[256];
        std::snprintf(line, sizeof line, "  %-28s %s\n", std::string(command.usage).c_str(),
                      std::string(command.help).c_str());
        out << line;
    }
    out << "\nAn empty line repeats the last command, so Enter keeps stepping.\n";
    context.print(out.str());
    return CmdResult::success();
}

CmdResult cmd_break(CmdContext& context, const std::vector<std::string>& args) {
    dbg::DebugSession& session = context.session;
    if (!session.has_program()) return CmdResult::failure("no program loaded");

    const bool temporary = args[0] == "tbreak";
    if (args.size() < 2) {
        // No argument: break on the current line, which is what you want after
        // stepping somewhere interesting.
        if (const auto line = session.current_line()) {
            if (session.breakpoints().add_line(*line, session.map(), temporary) != nullptr) {
                context.print("breakpoint at line " + std::to_string(*line) + "\n");
                return CmdResult::success();
            }
        }
        return CmdResult::failure("usage: break <line|symbol|*address>");
    }

    // `break *0x24` sets one on a raw address rather than a line.
    if (args[1][0] == '*') {
        const auto addr = resolve_address(session, args[1].substr(1));
        if (!addr) return CmdResult::failure(cannot_resolve(context.session, args[1].substr(1)));
        const dbg::Breakpoint* breakpoint = session.breakpoints().add_addr(*addr, session.map());
        char message[128];
        std::snprintf(message, sizeof message, "breakpoint %u at 0x%08x\n", breakpoint->id,
                      breakpoint->addr);
        context.print(message);
        return CmdResult::success();
    }

    // A symbol names an address; a bare number names a line.
    u32 line = 0;
    if (const auto symbol = session.lookup_symbol(args[1])) {
        const auto mapped = session.map().line_of(*symbol);
        if (!mapped) return CmdResult::failure("'" + args[1] + "' is not in executable code");
        line = *mapped;
    } else {
        char* end = nullptr;
        const long value = std::strtol(args[1].c_str(), &end, 0);
        if (end == args[1].c_str() || *end != '\0' || value <= 0) {
            std::string hint;
            const std::vector<std::string_view> names = session.symbols().names();
            if (const auto suggestion = as::closest_match(args[1], names); !suggestion.empty()) {
                hint = " (did you mean '" + std::string(suggestion) + "'?)";
            }
            return CmdResult::failure("not a line number or known symbol: " + args[1] + hint);
        }
        line = static_cast<u32>(value);
    }

    const dbg::Breakpoint* breakpoint =
        session.breakpoints().add_line(line, session.map(), temporary);
    if (breakpoint == nullptr) {
        return CmdResult::failure("no executable code at or after line " + std::to_string(line));
    }

    std::ostringstream out;
    out << (temporary ? "temporary breakpoint " : "breakpoint ") << breakpoint->id << " at line "
        << breakpoint->line;
    if (breakpoint->snapped) {
        out << " (line " << breakpoint->requested_line << " has no code, so it moved here)";
    }
    out << "\n";
    context.print(out.str());
    return CmdResult::success();
}

CmdResult cmd_watch(CmdContext& context, const std::vector<std::string>& args) {
    if (args.size() < 2) return CmdResult::failure("usage: watch <address|symbol>");
    const auto addr = resolve_address(context.session, args[1]);
    if (!addr) return CmdResult::failure(cannot_resolve(context.session, args[1]));
    const u32 current = context.session.read_dmem_word(*addr & ~3u);
    const dbg::Watchpoint* watchpoint = context.session.breakpoints().add_watch(*addr & ~3u, current);
    char message[128];
    std::snprintf(message, sizeof message, "watchpoint %u on 0x%08x (currently 0x%08x)\n",
                  watchpoint->id, watchpoint->addr, current);
    context.print(message);
    return CmdResult::success();
}

CmdResult cmd_delete(CmdContext& context, const std::vector<std::string>& args) {
    if (args.size() < 2) {
        context.session.breakpoints().clear();
        context.print("all breakpoints deleted\n");
        return CmdResult::success();
    }
    for (std::size_t i = 1; i < args.size(); ++i) {
        const auto id = static_cast<u32>(std::strtoul(args[i].c_str(), nullptr, 0));
        if (!context.session.breakpoints().remove(id)) {
            return CmdResult::failure("no breakpoint with id " + args[i]);
        }
    }
    return CmdResult::success();
}

CmdResult cmd_info(CmdContext& context, const std::vector<std::string>& args) {
    const std::string topic = args.size() > 1 ? args[1] : "registers";
    dbg::DebugSession& session = context.session;

    if (topic == "registers" || topic == "reg" || topic == "r") {
        context.print(format_registers(session, context.format));
        return CmdResult::success();
    }
    if (topic == "break" || topic == "breakpoints" || topic == "b") {
        context.print(format_breakpoints(session));
        return CmdResult::success();
    }
    if (topic == "symbols" || topic == "sym" || topic == "s") {
        context.print(format_symbols(session));
        return CmdResult::success();
    }
    if (topic == "frame" || topic == "line") {
        context.print(format_location(session, context.format));
        return CmdResult::success();
    }
    if (topic == "uart" || topic == "out") {
        const std::string& output = session.uart_output();
        context.print(output.empty() ? "(no uart output)\n" : output + "\n");
        return CmdResult::success();
    }
    if (topic == "mem" || topic == "memory") {
        char line[192];
        std::snprintf(line, sizeof line,
                      "imem  0x00000000..0x%08x  %zu instructions loaded\n"
                      "dmem  0x00000000..0x%08x  %zu bytes of initial data\n",
                      session.hart().imem().size() - 1, session.program().map.entries().size(),
                      session.hart().bus().dmem().size() - 1, session.program().dmem_bytes.size());
        std::string summary = line;

        // The peripherals the machine actually has, asked for rather than
        // assumed: a board file replaces the default set wholesale, so a fixed
        // list would name devices that are not there and miss the ones that
        // are -- which is the same failure as a device that quietly stopped
        // answering, only printed.
        const std::vector<const core::Device*> devices = [&] {
            const std::vector<core::Device*> mutable_devices = session.hart().bus().devices();
            return std::vector<const core::Device*>(mutable_devices.begin(),
                                                    mutable_devices.end());
        }();
        if (devices.empty()) {
            summary += "mmio  (nothing attached)\n";
        }
        for (const core::Device* device : devices) {
            const Addr base = device->base_address();
            const Addr end =
                base + static_cast<Addr>(std::max<std::size_t>(1, device->slot_count())) *
                           core::kSlotSize - 1;
            const std::string name = device->display_name();
            const std::string type(device->type_name());
            std::snprintf(line, sizeof line, "mmio  0x%08x..0x%08x  %s%s%s%s\n", base, end,
                          name.c_str(), name == type ? "" : " (", name == type ? "" : type.c_str(),
                          name == type ? "" : ")");
            summary += line;
        }
        context.print(summary);
        return CmdResult::success();
    }
    return CmdResult::failure("unknown topic '" + topic +
                              "'; try registers, break, symbols, frame, uart, mem");
}

CmdResult cmd_run(CmdContext& context, const std::vector<std::string>&) {
    if (!context.session.has_program()) return CmdResult::failure("no program loaded");
    context.session.reset();
    report(context, context.session.run());
    return CmdResult::success();
}

CmdResult cmd_continue(CmdContext& context, const std::vector<std::string>&) {
    if (!context.session.has_program()) return CmdResult::failure("no program loaded");
    report(context, context.session.run());
    return CmdResult::success();
}

CmdResult cmd_reset(CmdContext& context, const std::vector<std::string>&) {
    context.session.reset();
    context.print("machine reset; breakpoints kept\n");
    context.print(format_location(context.session, context.format));
    return CmdResult::success();
}

CmdResult cmd_list(CmdContext& context, const std::vector<std::string>& args) {
    dbg::DebugSession& session = context.session;
    if (session.source().line_count() == 0) return CmdResult::failure("no source loaded");

    u32 line = session.current_line().value_or(1);
    if (args.size() > 1) {
        if (const auto value = resolve_value(session, args[1])) line = static_cast<u32>(*value);
    }
    context.print(format_source(session, line));
    return CmdResult::success();
}

CmdResult cmd_examine(CmdContext& context, const std::vector<std::string>& args) {
    // Accepts both `x/16xw addr` and `x 16 addr`, because the gdb spelling is
    // unmemorable until you have used it a hundred times.
    ExamineSpec spec;
    std::size_t next = 1;
    if (args.size() > 1 && args[1][0] == '/') {
        spec = parse_examine_spec(args[1].substr(1));
        next = 2;
    }
    if (args.size() <= next) return CmdResult::failure("usage: x/<count><format><unit> <address>");

    const auto addr = resolve_address(context.session, args[next]);
    if (!addr) return CmdResult::failure(cannot_resolve(context.session, args[next]));
    context.print(examine(context.session, *addr, spec, context.format));
    return CmdResult::success();
}

CmdResult cmd_print(CmdContext& context, const std::vector<std::string>& args) {
    if (args.size() < 2) return CmdResult::failure("usage: print <register|symbol|number>");

    // A register prints in full, with both its names and its signed reading.
    if (const auto reg = isa::parse_register(args[1][0] == '$' ? args[1].substr(1) : args[1])) {
        context.print(format_register(context.session, *reg, context.format) + "\n");
        return CmdResult::success();
    }

    const auto value = resolve_value(context.session, args[1]);
    if (!value) return CmdResult::failure(cannot_resolve(context.session, args[1]));

    char message[192];
    if (const as::Symbol* symbol = context.session.symbols().find(args[1])) {
        std::snprintf(message, sizeof message, "%s = 0x%08x  (%s, defined at line %u)\n",
                      args[1].c_str(), static_cast<u32>(*value), as::space_name(symbol->space),
                      symbol->definition.line);
    } else {
        std::snprintf(message, sizeof message, "0x%08x  %lld\n", static_cast<u32>(*value),
                      static_cast<long long>(*value));
    }
    context.print(message);
    return CmdResult::success();
}

CmdResult cmd_set(CmdContext& context, const std::vector<std::string>& args) {
    if (args.size() < 3) return CmdResult::failure("usage: set <register> <value>");
    const std::string name = args[1][0] == '$' ? args[1].substr(1) : args[1];

    const auto value = resolve_value(context.session, args[2]);
    if (!value) return CmdResult::failure(cannot_resolve(context.session, args[2]));

    if (name == "pc") {
        context.session.hart().cpu().pc = static_cast<Addr>(*value);
        context.print(format_location(context.session, context.format));
        return CmdResult::success();
    }
    const auto reg = isa::parse_register(name);
    if (!reg) return CmdResult::failure("not a register: " + args[1]);
    if (*reg == 0) return CmdResult::failure("x0 is hardwired to zero and cannot be written");

    context.session.hart().cpu().x[*reg] = static_cast<u32>(*value);
    context.print(format_register(context.session, *reg, context.format) + "\n");
    return CmdResult::success();
}

CmdResult cmd_disassemble(CmdContext& context, const std::vector<std::string>& args) {
    Addr addr = context.session.cpu().pc;
    u32 count = 10;
    if (args.size() > 1) {
        const auto resolved = resolve_address(context.session, args[1]);
        if (!resolved) return CmdResult::failure(cannot_resolve(context.session, args[1]));
        addr = *resolved;
    }
    if (args.size() > 2) count = static_cast<u32>(std::strtoul(args[2].c_str(), nullptr, 0));
    context.print(format_disassembly(context.session, addr & ~1u, count, context.format));
    return CmdResult::success();
}

CmdResult cmd_quit(CmdContext& context, const std::vector<std::string>&) {
    context.quit = true;
    return CmdResult::success();
}

}  // namespace

// ---------------------------------------------------------------------------
// Registry
// ---------------------------------------------------------------------------

std::vector<std::string> tokenize_command(std::string_view line) {
    std::vector<std::string> tokens;
    std::size_t position = 0;
    while (position < line.size()) {
        while (position < line.size() &&
               std::isspace(static_cast<unsigned char>(line[position])) != 0) {
            ++position;
        }
        const std::size_t start = position;
        while (position < line.size() &&
               std::isspace(static_cast<unsigned char>(line[position])) == 0) {
            ++position;
        }
        if (position > start) tokens.emplace_back(line.substr(start, position - start));
    }
    return tokens;
}

CommandRegistry::CommandRegistry() {
    commands_ = {
        {"help", {"h", "?"}, "help [command]", "list commands, or explain one", cmd_help},

        {"break", {"b"}, "break <line|symbol|*addr>",
         "stop at a source line; a line with no code snaps to the next one that has some",
         cmd_break},
        {"tbreak", {}, "tbreak <line|symbol>", "one-shot breakpoint", cmd_break},
        {"watch", {"w"}, "watch <addr|symbol>", "stop when a data word changes", cmd_watch},
        {"delete", {"d"}, "delete [id...]", "remove breakpoints, or all of them", cmd_delete},

        {"run", {"r"}, "run", "reset and run from the entry point", cmd_run},
        {"continue", {"c"}, "continue", "resume until something stops it", cmd_continue},
        {"step", {"s"}, "step", "one source line, stepping into calls",
         [](CmdContext& context, const std::vector<std::string>&) {
             return do_step(context, StepKind::Line);
         }},
        {"stepi", {"si"}, "stepi", "one machine instruction -- shows what a pseudo expands to",
         [](CmdContext& context, const std::vector<std::string>&) {
             return do_step(context, StepKind::Instruction);
         }},
        {"next", {"n"}, "next", "one source line, running calls to completion",
         [](CmdContext& context, const std::vector<std::string>&) {
             return do_step(context, StepKind::LineOver);
         }},
        {"finish", {"fin"}, "finish", "run until the current function returns",
         [](CmdContext& context, const std::vector<std::string>&) {
             return do_step(context, StepKind::Out);
         }},
        {"back", {"bs"}, "back", "undo one source line",
         [](CmdContext& context, const std::vector<std::string>&) {
             return do_reverse(context, StepKind::Line);
         }},
        {"backi", {"bsi"}, "backi", "undo one machine instruction",
         [](CmdContext& context, const std::vector<std::string>&) {
             return do_reverse(context, StepKind::Instruction);
         }},
        {"reset", {}, "reset", "restart the machine, keeping breakpoints", cmd_reset},

        {"info", {"i"}, "info <registers|break|symbols|frame|uart|mem>", "inspect machine state",
         cmd_info},
        {"list", {"l"}, "list [line]", "show source around a line", cmd_list},
        {"x", {}, "x/<count><fmt><unit> <addr>", "examine memory, e.g. x/16xw 0", cmd_examine},
        {"print", {"p"}, "print <register|symbol|number>", "show a value", cmd_print},
        {"set", {}, "set <register> <value>", "write a register", cmd_set},
        {"disassemble", {"dis"}, "disassemble [addr] [count]", "disassemble instructions",
         cmd_disassemble},

        {"quit", {"q", "exit"}, "quit", "leave the debugger", cmd_quit},
    };
}

const CommandRegistry& CommandRegistry::instance() {
    static const CommandRegistry registry;
    return registry;
}

const Command* CommandRegistry::find(std::string_view name) const {
    for (const Command& command : commands_) {
        if (command.name == name) return &command;
    }
    // Full names win over aliases, so a future command called "b" could not be
    // shadowed by break's alias.
    for (const Command& command : commands_) {
        for (const std::string_view alias : command.aliases) {
            if (alias == name) return &command;
        }
    }
    return nullptr;
}

std::vector<std::string_view> CommandRegistry::names() const {
    std::vector<std::string_view> out;
    for (const Command& command : commands_) {
        out.push_back(command.name);
        for (const std::string_view alias : command.aliases) out.push_back(alias);
    }
    return out;
}

CmdResult CommandRegistry::execute(CmdContext& context, std::string_view line) const {
    std::vector<std::string> args = tokenize_command(line);
    if (args.empty()) return CmdResult::success();

    // `x/16xw addr` is one whitespace token but two arguments. Splitting at the
    // slash here means the format spec reaches the command the same way whether
    // it was written attached or separated.
    if (const std::size_t slash = args[0].find('/'); slash != std::string::npos && slash > 0) {
        args.insert(args.begin() + 1, args[0].substr(slash));
        args[0].erase(slash);
    }

    const Command* command = find(args[0]);
    if (command == nullptr) {
        std::string message = "unknown command '" + args[0] + "'";
        if (const auto suggestion = as::closest_match(args[0], names()); !suggestion.empty()) {
            message += " (did you mean '" + std::string(suggestion) + "'?)";
        }
        return CmdResult::failure(message + "; type 'help' for the list");
    }
    return command->run(context, args);
}

}  // namespace rv::cmd
