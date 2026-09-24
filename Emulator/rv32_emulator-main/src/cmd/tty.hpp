// What the terminal on the other end can do.
//
// This is the only file in the project that asks the operating system
// anything. Keeping the two questions here means the rest of the CLI is plain
// C++17 -- porting it is a matter of answering these correctly rather than
// auditing every fputs for escape codes.
#pragma once

#include <cstdio>

namespace rv::cmd {

/// True when `stream` is a terminal rather than a file or a pipe. The debugger
/// keys interactivity off this: a person gets a prompt, a piped script gets its
/// commands echoed so the output makes sense on its own.
bool is_terminal(std::FILE* stream);

/// True when ANSI colour escapes will render on `stream`.
///
/// On POSIX that is the same question as `is_terminal`. A Windows console has
/// to be *asked* to interpret escapes, so this does the asking -- and answers
/// false if the console refuses, which is what keeps diagnostics on an old
/// conhost readable rather than strewn with `<-[31m`.
bool supports_color(std::FILE* stream);

}  // namespace rv::cmd
