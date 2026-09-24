// The command registry.
//
// This layer sits *below* both front-ends. `break`, `step` and `x/16xw` are
// implemented once here; the CLI reads lines and feeds them in, and the TUI's
// ':' prompt feeds the same strings to the same code. Nothing gets implemented
// twice, and the two interfaces cannot drift apart.
//
// Note that this library must not depend on ncurses.
#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "cmd/format.hpp"
#include "dbg/session.hpp"

namespace rv::cmd {

struct CmdContext {
    dbg::DebugSession& session;
    /// Where output goes. The CLI writes to stdout; the TUI appends to its
    /// message pane.
    std::function<void(const std::string&)> emit;
    FormatOptions format;
    bool quit = false;

    void print(const std::string& text) {
        if (emit) emit(text);
    }
};

struct CmdResult {
    bool ok = true;
    std::string error;

    static CmdResult success() { return {}; }
    static CmdResult failure(std::string message) { return CmdResult{false, std::move(message)}; }
};

struct Command {
    std::string_view name;
    /// Short aliases, e.g. "b" for break. Checked after full names.
    std::vector<std::string_view> aliases;
    std::string_view usage;
    std::string_view help;
    std::function<CmdResult(CmdContext&, const std::vector<std::string>&)> run;
};

class CommandRegistry {
public:
    static const CommandRegistry& instance();

    const Command* find(std::string_view name) const;
    const std::vector<Command>& all() const { return commands_; }

    /// Split a line into a command and its arguments, then run it. An empty
    /// line repeats the previous command, as gdb does -- pressing Enter to
    /// step again is the single most used key in a debugger.
    CmdResult execute(CmdContext& context, std::string_view line) const;

    /// Names for the "did you mean" suggester.
    std::vector<std::string_view> names() const;

private:
    CommandRegistry();
    std::vector<Command> commands_;
};

/// Split on whitespace, honouring nothing else -- assembly debugger commands
/// have no quoting needs.
std::vector<std::string> tokenize_command(std::string_view line);

}  // namespace rv::cmd
