// Plain-text renderers for machine state.
//
// These live below both front-ends. The CLI prints the strings directly; the
// TUI reuses the same layout decisions when drawing its panels, so `info reg`
// and the register panel agree about what a register looks like.
#pragma once

#include <string>

#include "dbg/session.hpp"

namespace rv::cmd {

struct FormatOptions {
    /// Show `x10` instead of `a0`. Both name the same register; the UI shows
    /// both side by side, but a line of text has to choose.
    bool numeric_registers = false;
    /// Width available, in columns. The register dump packs itself to fit.
    int width = 80;
    /// Mark registers the last step changed.
    bool highlight_changes = true;
};

/// All 32 registers plus pc and the counters.
std::string format_registers(const dbg::DebugSession& session, const FormatOptions& options = {});

/// A single register line, for `print a0`.
std::string format_register(const dbg::DebugSession& session, RegIdx reg,
                            const FormatOptions& options = {});

/// A hex dump. `word_count` words from `addr`, in data memory.
std::string format_memory(const dbg::DebugSession& session, Addr addr, u32 word_count,
                          const FormatOptions& options = {});

/// Disassembly with the source line interleaved, and markers for the pc and
/// for breakpoints.
std::string format_disassembly(const dbg::DebugSession& session, Addr addr, u32 count,
                               const FormatOptions& options = {});

/// Source lines around `line`, with a marker on the current one.
std::string format_source(const dbg::DebugSession& session, u32 line, u32 radius = 5);

/// One line describing where and why execution stopped.
std::string format_stop(const dbg::DebugSession& session, const dbg::StopEvent& event);

/// The single line of context a debugger prints after every step: the address,
/// the disassembly, and the source line it came from.
std::string format_location(const dbg::DebugSession& session,
                            const FormatOptions& options = {});

std::string format_breakpoints(const dbg::DebugSession& session);
std::string format_symbols(const dbg::DebugSession& session);

}  // namespace rv::cmd
