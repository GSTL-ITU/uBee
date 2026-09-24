#include "cmd/tty.hpp"

#ifdef _WIN32
#include <cstdint>

#include <io.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
// Present since Windows 10, but not in every SDK this might be built against.
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif
#else
#include <unistd.h>
#endif

namespace rv::cmd {

#ifdef _WIN32
namespace {

/// Switch the console attached to `stream` into ANSI mode. Windows Terminal is
/// already there, in which case this changes nothing and still reports true;
/// conhost has to be told, and a console old enough to refuse gets no colour.
bool enable_virtual_terminal(std::FILE* stream) {
    const int fd = _fileno(stream);
    if (fd < 0) return false;

    // Checked before the cast rather than against INVALID_HANDLE_VALUE, whose
    // definition is a C-style cast -- and -Wold-style-cast is an error here.
    const std::intptr_t raw = _get_osfhandle(fd);
    if (raw == -1 || raw == -2) return false;
    const auto handle = reinterpret_cast<HANDLE>(raw);

    DWORD mode = 0;
    if (GetConsoleMode(handle, &mode) == 0) return false;
    if ((mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0) return true;
    return SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
}

}  // namespace
#endif

bool is_terminal(std::FILE* stream) {
#ifdef _WIN32
    return _isatty(_fileno(stream)) != 0;
#else
    return isatty(fileno(stream)) != 0;
#endif
}

bool supports_color(std::FILE* stream) {
    if (!is_terminal(stream)) return false;
#ifdef _WIN32
    return enable_virtual_terminal(stream);
#else
    return true;
#endif
}

}  // namespace rv::cmd
