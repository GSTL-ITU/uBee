#include "cmd/format.hpp"

#include <cstdio>
#include <sstream>

#include "isa/disasm.hpp"
#include "isa/regnames.hpp"

namespace rv::cmd {
namespace {

std::string hex32(u32 value) {
    char buffer[16];
    std::snprintf(buffer, sizeof buffer, "%08x", value);
    return buffer;
}

/// An instruction word at the width it actually occupies, right-aligned in the
/// same eight columns so a mixed listing stays in line.
std::string hex_instr(u32 value, u8 length) {
    char buffer[16];
    if (length == 2) {
        std::snprintf(buffer, sizeof buffer, "    %04x", value & 0xffffu);
    } else {
        std::snprintf(buffer, sizeof buffer, "%08x", value);
    }
    return buffer;
}

bool changed_recently(const dbg::DebugSession& session, RegIdx reg) {
    for (const dbg::RegChange& change : session.last_changes()) {
        if (change.index == reg) return true;
    }
    return false;
}

isa::DisasmOptions disasm_options(const FormatOptions& options) {
    isa::DisasmOptions out;
    out.abi_names = !options.numeric_registers;
    return out;
}

}  // namespace

std::string format_register(const dbg::DebugSession& session, RegIdx reg,
                            const FormatOptions& options) {
    const u32 value = session.cpu().x[reg];
    char buffer[128];
    // Both spellings, because the encoding contains the number and real
    // assembly is written with the name.
    std::snprintf(buffer, sizeof buffer, "%-3s %-4s 0x%08x  %11d%s",
                  isa::numeric_name(reg).data(), isa::abi_name(reg).data(), value,
                  static_cast<i32>(value),
                  (options.highlight_changes && changed_recently(session, reg)) ? "  <-- changed"
                                                                               : "");
    return buffer;
}

std::string format_registers(const dbg::DebugSession& session, const FormatOptions& options) {
    // Four per row fits 80 columns: 13 characters each plus separators.
    const int per_row = options.width >= 110 ? 4 : (options.width >= 80 ? 3 : 2);
    std::ostringstream out;

    for (RegIdx reg = 0; reg < kNumRegs; ++reg) {
        const u32 value = session.cpu().x[reg];
        const bool marked = options.highlight_changes && changed_recently(session, reg);
        char cell[64];
        // The change marker leads its own cell, so the trailing space matters:
        // without it the '*' would sit against the previous register's value
        // and look as though it belonged to that one.
        std::snprintf(cell, sizeof cell, "%c%-4s %-8s ", marked ? '*' : ' ',
                      options.numeric_registers ? isa::numeric_name(reg).data()
                                                : isa::abi_name(reg).data(),
                      hex32(value).c_str());
        out << cell;
        if ((reg + 1) % per_row == 0) out << "\n";
    }
    if (kNumRegs % per_row != 0) out << "\n";

    const core::CpuState& cpu = session.cpu();
    char tail[128];
    std::snprintf(tail, sizeof tail, " pc    %s   cycle %llu   instret %llu",
                  hex32(cpu.pc).c_str(), static_cast<unsigned long long>(cpu.cycle),
                  static_cast<unsigned long long>(cpu.instret));
    out << tail << "\n";
    return out.str();
}

std::string format_memory(const dbg::DebugSession& session, Addr addr, u32 word_count,
                          const FormatOptions& options) {
    // Four words per row is 8 + 2 + 4*9 + 2 + 16 = 64 columns, which leaves
    // room at 80 and reads better than cramming in more.
    const u32 per_row = options.width >= 100 ? 8 : 4;
    std::ostringstream out;

    const Addr start = addr & ~3u;
    for (u32 index = 0; index < word_count; index += per_row) {
        const Addr row_addr = start + index * 4;
        out << hex32(row_addr) << ": ";

        std::string ascii;
        for (u32 column = 0; column < per_row && index + column < word_count; ++column) {
            const u32 value = session.read_dmem_word(row_addr + column * 4);
            out << hex32(value) << " ";
            for (int byte = 0; byte < 4; ++byte) {
                const auto character = static_cast<char>((value >> (8 * byte)) & 0xffu);
                ascii.push_back(character >= 0x20 && character < 0x7f ? character : '.');
            }
        }
        out << " |" << ascii << "|\n";
    }
    return out.str();
}

std::string format_disassembly(const dbg::DebugSession& session, Addr addr, u32 count,
                               const FormatOptions& options) {
    std::ostringstream out;
    const Addr pc = session.cpu().pc;
    u32 last_line = 0;

    // Instructions are a halfword stream, so the listing cannot stride: each one
    // has to be decoded to learn how far the next one starts.
    Addr at = addr;
    for (u32 index = 0; index < count; ++index, at += session.instr_length_at(at)) {
        const Word word = session.read_imem_instr(at);
        const auto entry = session.map().at(at);

        // Print the source line above the first instruction it produced, so a
        // pseudo-instruction's expansion sits visibly underneath it.
        if (entry.has_value() && entry->slot == 0 && entry->line != last_line) {
            const std::string_view text = session.source().line(entry->line);
            if (!text.empty()) {
                char header[256];
                std::snprintf(header, sizeof header, "%5u  %.*s", entry->line,
                              static_cast<int>(text.size()), text.data());
                out << header << "\n";
            }
            last_line = entry->line;
        }

        const bool has_breakpoint = session.breakpoints().at(at) != nullptr;
        // A continuation word of a multi-instruction pseudo gets a corner mark,
        // so it is clear it did not come from a line of its own.
        const bool continuation = entry.has_value() && entry->slot > 0;

        char line[256];
        std::snprintf(line, sizeof line, "  %c%s %s  %s  %s", has_breakpoint ? 'B' : ' ',
                      at == pc ? ">" : " ", hex32(at).c_str(),
                      hex_instr(word, session.instr_length_at(at)).c_str(),
                      continuation ? "  " : "");
        out << line << isa::disassemble_word(word, at, disasm_options(options)) << "\n";
    }
    return out.str();
}

std::string format_source(const dbg::DebugSession& session, u32 line, u32 radius) {
    std::ostringstream out;
    const u32 first = line > radius ? line - radius : 1;
    const auto total = static_cast<u32>(session.source().line_count());
    const u32 last = std::min(total, line + radius);

    for (u32 index = first; index <= last; ++index) {
        const std::string_view text = session.source().line(index);
        const bool has_breakpoint = session.breakpoints().has_breakpoint_on_line(index);
        char prefix[32];
        std::snprintf(prefix, sizeof prefix, "%c%s %4u  ", has_breakpoint ? 'B' : ' ',
                      index == line ? ">" : " ", index);
        out << prefix << text << "\n";
    }
    return out.str();
}

std::string format_location(const dbg::DebugSession& session, const FormatOptions& options) {
    const Addr pc = session.cpu().pc;
    // Fetch the way the hart does. A plain word read happens to disassemble a
    // compressed instruction correctly -- decode() ignores the upper half --
    // but it returns 0 for one sitting in the last halfword of imem.
    const Word word = session.read_imem_instr(pc);
    std::ostringstream out;

    out << "=> " << hex32(pc) << "  " << isa::disassemble_word(word, pc, disasm_options(options));

    if (const auto entry = session.map().at(pc)) {
        const std::string_view text = session.source().line(entry->line);
        char suffix[128];
        if (entry->count > 1) {
            // Make a multi-instruction pseudo visible rather than mysterious.
            std::snprintf(suffix, sizeof suffix, "        [line %u, %u/%u]", entry->line,
                          entry->slot + 1, entry->count);
        } else {
            std::snprintf(suffix, sizeof suffix, "        [line %u]", entry->line);
        }
        out << suffix;
        if (!text.empty()) {
            std::size_t indent = text.find_first_not_of(" \t");
            if (indent == std::string_view::npos) indent = 0;
            out << "\n   " << text.substr(indent);
        }
    }
    out << "\n";
    return out.str();
}

std::string format_stop(const dbg::DebugSession& session, const dbg::StopEvent& event) {
    std::ostringstream out;
    switch (event.reason) {
        case dbg::StopReason::Step:
            return {};
        case dbg::StopReason::Breakpoint:
            out << "breakpoint " << event.breakpoint_id << " at 0x" << hex32(event.pc);
            if (event.line) out << " (line " << *event.line << ")";
            break;
        case dbg::StopReason::Watchpoint:
            out << "watchpoint at 0x" << hex32(event.watch_addr) << ": 0x"
                << hex32(event.watch_old) << " -> 0x" << hex32(event.watch_new);
            break;
        case dbg::StopReason::Halted:
            out << "stopped: " << core::halt_reason_name(event.halt);
            if (event.halt == core::HaltReason::UnhandledTrap) {
                out << " (" << core::trap_cause_name(event.cause) << " at 0x" << hex32(event.pc)
                    << ")";
            }
            break;
        case dbg::StopReason::Trap:
            out << "trap: " << core::trap_cause_name(event.cause);
            break;
        case dbg::StopReason::Watchdog:
            out << "stopped after " << session.options().watchdog
                << " instructions -- infinite loop?";
            break;
        case dbg::StopReason::NotRunning:
            out << "no program loaded";
            break;
        case dbg::StopReason::HistoryHorizon:
            out << "no further history: this is as far back as the recording goes";
            break;
    }
    out << "\n";
    return out.str();
}

std::string format_breakpoints(const dbg::DebugSession& session) {
    const auto& breakpoints = session.breakpoints().breakpoints();
    const auto& watchpoints = session.breakpoints().watchpoints();
    if (breakpoints.empty() && watchpoints.empty()) return "no breakpoints\n";

    std::ostringstream out;
    out << "  id  type   where                    hits\n";
    for (const dbg::Breakpoint& breakpoint : breakpoints) {
        char line[192];
        std::snprintf(line, sizeof line, "%4u  break  line %-5u  0x%08x %s  %4u\n", breakpoint.id,
                      breakpoint.line, breakpoint.addr, breakpoint.enabled ? "  " : "(off)",
                      breakpoint.hit_count);
        out << line;
        if (breakpoint.snapped) {
            char note[128];
            std::snprintf(note, sizeof note,
                          "      (asked for line %u, which has no code; moved to %u)\n",
                          breakpoint.requested_line, breakpoint.line);
            out << note;
        }
    }
    for (const dbg::Watchpoint& watchpoint : watchpoints) {
        char line[128];
        std::snprintf(line, sizeof line, "%4u  watch  0x%08x = 0x%08x\n", watchpoint.id,
                      watchpoint.addr, watchpoint.last_value);
        out << line;
    }
    return out.str();
}

std::string format_symbols(const dbg::DebugSession& session) {
    const auto& symbols = session.symbols().all();
    if (symbols.empty()) return "no symbols\n";

    std::ostringstream out;
    out << "  space     value       line  name\n";
    for (const as::Symbol& symbol : symbols) {
        char line[256];
        std::snprintf(line, sizeof line, "  %-8s  0x%08x  %5u  %s%s\n",
                      as::space_name(symbol.space), symbol.value, symbol.definition.line,
                      symbol.name.c_str(), symbol.is_global ? "  (global)" : "");
        out << line;
    }
    return out.str();
}

}  // namespace rv::cmd
