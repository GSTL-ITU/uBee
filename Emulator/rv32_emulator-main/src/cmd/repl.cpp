#include "cmd/repl.hpp"

#include <istream>
#include <ostream>

namespace rv::cmd {
namespace {

constexpr const char* kReset = "\033[0m";
constexpr const char* kRed = "\033[31m";
constexpr const char* kBlue = "\033[34m";

std::string strip(const std::string& text) {
    const std::size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const std::size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

}  // namespace

Repl::Repl(dbg::DebugSession& session, ReplOptions options)
    : session_(session), options_(std::move(options)) {}

void Repl::execute_line(const std::string& line, std::ostream& output) {
    std::string command = strip(line);

    // An empty line repeats the previous command. This is the most used key in
    // any debugger: you press Enter over and over to keep stepping.
    if (command.empty()) {
        if (last_command_.empty()) return;
        command = last_command_;
        if (options_.echo) output << options_.prompt << command << "\n";
    } else {
        // Comments let a scripted session explain itself.
        if (command[0] == '#') return;
        last_command_ = command;
        if (options_.echo) output << options_.prompt << command << "\n";
    }

    CmdContext context{session_, [&output](const std::string& text) { output << text; },
                       options_.format, false};

    const CmdResult result = CommandRegistry::instance().execute(context, command);
    if (!result.ok) {
        if (options_.color) output << kRed;
        output << "error: " << result.error;
        if (options_.color) output << kReset;
        output << "\n";
    }
    if (context.quit) quit_ = true;
}

int Repl::run(std::istream& input, std::ostream& output) {
    std::string line;
    while (!quit_) {
        // When echoing, execute_line prints the prompt together with the
        // command it is about to run, so printing one here as well would
        // double it up on every line of a piped session.
        if (!options_.echo) {
            if (options_.color) output << kBlue;
            output << options_.prompt;
            if (options_.color) output << kReset;
            output.flush();
        }

        if (!std::getline(input, line)) {
            output << "\n";
            break;
        }
        execute_line(line, output);
    }
    return 0;
}

}  // namespace rv::cmd
