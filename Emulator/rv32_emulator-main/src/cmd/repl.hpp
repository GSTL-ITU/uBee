// A line-based debugger REPL.
//
// Deliberately free of ncurses: this is the front-end you can pipe a script
// into, redirect to a file, or run over ssh on a terminal that cannot do
// anything clever. The TUI reuses the command registry underneath it rather
// than any of this.
#pragma once

#include <iosfwd>
#include <string>

#include "cmd/command.hpp"
#include "dbg/session.hpp"

namespace rv::cmd {

struct ReplOptions {
    std::string prompt = "(rv32) ";
    /// Colour the prompt and errors. Off when stdout is not a terminal.
    bool color = false;
    /// Echo each command before running it, so a piped script's output makes
    /// sense on its own.
    bool echo = false;
    FormatOptions format;
};

class Repl {
public:
    Repl(dbg::DebugSession& session, ReplOptions options = {});

    /// Read lines until end of input or `quit`. Returns the process exit code.
    int run(std::istream& input, std::ostream& output);

    /// Execute one line, as if typed. Exposed so a session can be scripted
    /// from a test or from `--command` arguments.
    void execute_line(const std::string& line, std::ostream& output);

    bool should_quit() const { return quit_; }

private:
    dbg::DebugSession& session_;
    ReplOptions options_;
    /// The last non-empty command, so pressing Enter repeats it. This state
    /// belongs to the REPL rather than to the shared registry, which is a
    /// const singleton and must stay one.
    std::string last_command_;
    bool quit_ = false;
};

}  // namespace rv::cmd
