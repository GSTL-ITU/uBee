// A dependency-free test harness.
//
// gtest/catch2 are not installed and we do not assume network access, so this
// is the whole testing framework. It is deliberately small: self-registering
// test cases, a handful of check macros, and a runner with filtering.
//
//   RV_TEST(decode, addi) {
//       auto d = decode(0x00500093);
//       RV_CHECK_EQ(d.rd, 1);
//       RV_CHECK_HEX(d.imm, 5);
//   }
#pragma once

#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

namespace rvtest {

struct TestCase {
    const char* suite;
    const char* name;
    void (*fn)();
    const char* file;
    int line;
};

/// Function-local static, so registration order never depends on translation
/// unit initialisation order.
inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> cases;
    return cases;
}

struct Registrar {
    Registrar(const char* suite, const char* name, void (*fn)(), const char* file, int line) {
        registry().push_back(TestCase{suite, name, fn, file, line});
    }
};

inline int& failures_in_current_test() {
    static int count = 0;
    return count;
}

/// Render a value for a failure message. Integers get decimal *and* hex,
/// because half the assertions in this project compare 32-bit machine words.
template <typename T>
std::string display(const T& value) {
    std::ostringstream os;
    if constexpr (std::is_same_v<T, bool>) {
        os << (value ? "true" : "false");
    } else if constexpr (std::is_integral_v<T>) {
        // Promote through a wide type so u8/i8 print as numbers, not characters.
        const long long as_signed = static_cast<long long>(value);
        const unsigned long long as_unsigned = static_cast<unsigned long long>(
            static_cast<std::make_unsigned_t<T>>(value));
        os << as_signed << " (0x" << std::hex << std::uppercase << as_unsigned << ")";
    } else if constexpr (std::is_enum_v<T>) {
        os << static_cast<long long>(value);
    } else {
        os << value;
    }
    return os.str();
}

inline std::string display(const std::string& value) { return "\"" + value + "\""; }
inline std::string display(const char* value) { return std::string("\"") + value + "\""; }

inline void report_failure(const char* file, int line, const std::string& message) {
    ++failures_in_current_test();
    std::fprintf(stderr, "  \033[31mFAIL\033[0m %s:%d\n", file, line);
    std::fprintf(stderr, "       %s\n", message.c_str());
}

inline void check(bool condition, const char* expr, const char* file, int line) {
    if (!condition) report_failure(file, line, std::string("expected true: ") + expr);
}

template <typename A, typename B>
void check_eq(const A& actual, const B& expected, const char* actual_expr,
              const char* expected_expr, const char* file, int line) {
    if (actual == expected) return;
    report_failure(file, line, std::string(actual_expr) + " == " + expected_expr + "\n" +
                                   "         actual: " + display(actual) + "\n" +
                                   "       expected: " + display(expected));
}

template <typename A, typename B>
void check_ne(const A& actual, const B& forbidden, const char* actual_expr,
              const char* forbidden_expr, const char* file, int line) {
    if (!(actual == forbidden)) return;
    report_failure(file, line, std::string(actual_expr) + " != " + forbidden_expr +
                                   "\n         both are: " + display(actual));
}

/// Compare 32-bit words. Same as check_eq but formats as 0x%08x only, which is
/// far easier to eyeball than a decimal diff of two instruction encodings.
inline void check_hex(unsigned long long actual, unsigned long long expected,
                      const char* actual_expr, const char* expected_expr, const char* file,
                      int line) {
    if (actual == expected) return;
    char buffer[256];
    std::snprintf(buffer, sizeof buffer,
                  "%s == %s\n         actual: 0x%08llx\n       expected: 0x%08llx", actual_expr,
                  expected_expr, actual, expected);
    report_failure(file, line, buffer);
}

/// Multi-line string comparison that points at the first differing line, used
/// for disassembly, rendered diagnostics and golden TUI frames.
inline void check_str(const std::string& actual, const std::string& expected,
                      const char* actual_expr, const char* file, int line) {
    if (actual == expected) return;
    std::istringstream a(actual), e(expected);
    std::string a_line, e_line;
    int line_no = 1;
    std::ostringstream os;
    os << actual_expr << " mismatch";
    while (true) {
        const bool got_a = static_cast<bool>(std::getline(a, a_line));
        const bool got_e = static_cast<bool>(std::getline(e, e_line));
        if (!got_a && !got_e) break;
        if (a_line != e_line) {
            os << "\n       first difference at line " << line_no << ":"
               << "\n         actual: " << (got_a ? a_line : "<missing>")
               << "\n       expected: " << (got_e ? e_line : "<missing>");
            break;
        }
        ++line_no;
    }
    report_failure(file, line, os.str());
}

inline int run_all(int argc, char** argv) {
    const char* filter = nullptr;
    bool list_only = false;
    bool fail_fast = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strncmp(argv[i], "--filter=", 9) == 0) {
            filter = argv[i] + 9;
        } else if (std::strcmp(argv[i], "--list") == 0) {
            list_only = true;
        } else if (std::strcmp(argv[i], "--fail-fast") == 0) {
            fail_fast = true;
        } else {
            std::fprintf(stderr, "usage: %s [--filter=SUBSTR] [--list] [--fail-fast]\n", argv[0]);
            return 2;
        }
    }

    int passed = 0;
    int failed = 0;
    for (const TestCase& test : registry()) {
        const std::string full = std::string(test.suite) + "." + test.name;
        if (filter != nullptr && full.find(filter) == std::string::npos) continue;
        if (list_only) {
            std::printf("%s\n", full.c_str());
            continue;
        }
        failures_in_current_test() = 0;
        std::printf("\033[90m....\033[0m %s", full.c_str());
        std::fflush(stdout);
        test.fn();
        if (failures_in_current_test() == 0) {
            std::printf("\r\033[32m ok \033[0m %s\n", full.c_str());
            ++passed;
        } else {
            std::printf("\r\033[31mFAIL\033[0m %s\n", full.c_str());
            ++failed;
            if (fail_fast) break;
        }
    }

    if (list_only) return 0;
    std::printf("\n%d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}

}  // namespace rvtest

#define RV_TEST(suite, name)                                                        \
    static void rv_test_##suite##_##name();                                         \
    static const ::rvtest::Registrar rv_registrar_##suite##_##name{                 \
        #suite, #name, &rv_test_##suite##_##name, __FILE__, __LINE__};              \
    static void rv_test_##suite##_##name()

#define RV_CHECK(cond) ::rvtest::check((cond), #cond, __FILE__, __LINE__)
#define RV_CHECK_EQ(a, b) ::rvtest::check_eq((a), (b), #a, #b, __FILE__, __LINE__)
#define RV_CHECK_NE(a, b) ::rvtest::check_ne((a), (b), #a, #b, __FILE__, __LINE__)
#define RV_CHECK_HEX(a, b) ::rvtest::check_hex((a), (b), #a, #b, __FILE__, __LINE__)
#define RV_CHECK_STR(a, b) ::rvtest::check_str((a), (b), #a, __FILE__, __LINE__)
