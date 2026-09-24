// The debugger façade.
//
// The CLI, the TUI and the tests all talk to a DebugSession and to nothing
// below it. If a feature cannot be expressed as a method here, it does not
// belong in a user interface -- that rule is what keeps the two front-ends
// from growing their own divergent copies of "what stepping means".
#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "asm/assembler.hpp"
#include "core/hart.hpp"
#include "dbg/breakpoints.hpp"
#include "dbg/history.hpp"

namespace rv::dbg {

enum class StepKind : u8 {
    Instruction,  // one machine instruction -- the primitive
    Line,         // one source line, however many instructions that is
    LineOver,     // like Line, but runs calls to completion
    Out,          // run until the current function returns
};

enum class StopReason : u8 {
    Step,           // the requested amount of stepping simply finished
    Breakpoint,
    Watchpoint,
    Halted,         // ebreak, or an unhandled trap
    Trap,           // a trap was taken and handled; execution continues at mtvec
    Watchdog,       // instruction budget exhausted -- almost certainly a loop
    NotRunning,     // nothing loaded, or already halted
    HistoryHorizon, // reverse stepping ran out of recorded history
};

const char* stop_reason_name(StopReason reason);

struct StopEvent {
    StopReason reason = StopReason::Step;
    Addr pc = 0;
    std::optional<u32> line;
    u32 breakpoint_id = 0;
    Addr watch_addr = 0;
    u32 watch_old = 0;
    u32 watch_new = 0;
    core::HaltReason halt = core::HaltReason::None;
    core::TrapCause cause = core::TrapCause::None;
};

/// One instruction as it was recorded, for a view that needs to see what is
/// behind the pc without disturbing it.
struct RecentStep {
    Addr pc = 0;
    Word raw = 0;
};

/// One register that changed in the last step, for diff highlighting.
struct RegChange {
    RegIdx index = 0;
    u32 before = 0;
    u32 after = 0;
};

struct SessionOptions {
    /// Instructions to execute before giving up on a run. Students write
    /// infinite loops constantly; the terminal must never just hang.
    u64 watchdog = 10'000'000;
    std::size_t history_capacity = 1u << 16;
};

class DebugSession {
public:
    explicit DebugSession(SessionOptions options = {});

    // ---- loading ----------------------------------------------------------

    /// Assemble and load. Breakpoints survive, rebound by line number, which is
    /// what makes the editor's write-F5-debug loop bearable.
    bool load_source(std::string text, std::string name,
                     const as::AssembleOptions& options = {});
    /// Load a pre-assembled image, with no source to map back to.
    void load_image(const std::vector<Word>& imem, const std::vector<u8>& dmem = {});

    const as::AssembledProgram& program() const { return program_; }
    const as::SourceFile& source() const { return source_; }
    bool has_program() const { return loaded_; }

    // ---- execution --------------------------------------------------------

    void reset();
    StopEvent step(StepKind kind = StepKind::Line);
    StopEvent run();
    /// Execute at most `budget` instructions and return early if something
    /// stops it. The TUI calls this in slices so the keyboard stays responsive
    /// without a second thread.
    std::optional<StopEvent> run_chunk(u64 budget);
    StopEvent run_to(Addr addr);

    /// Undo one step. Reverse-Line pops until the mapped source line changes.
    StopEvent reverse_step(StepKind kind = StepKind::Line);
    bool can_reverse() const { return history_.can_undo(); }

    bool running() const { return loaded_ && !hart_.halted(); }
    bool halted() const { return hart_.halted(); }

    // ---- inspection -------------------------------------------------------

    const core::Hart& hart() const { return hart_; }
    core::Hart& hart() { return hart_; }
    const core::CpuState& cpu() const { return hart_.cpu(); }

    BreakpointSet& breakpoints() { return breakpoints_; }
    const BreakpointSet& breakpoints() const { return breakpoints_; }

    const as::SourceMap& map() const { return program_.map; }
    const as::SymbolTable& symbols() const { return program_.symbols; }

    std::optional<u32> current_line() const;
    /// Which word of its source line the pc sits on, as (slot, count). The
    /// status bar shows this as [1/2] so that a two-instruction `li` is
    /// visible rather than mysterious.
    std::optional<std::pair<u8, u8>> current_slot() const;

    /// Registers that changed in the last step. Read straight from the
    /// executor's record, not recovered by diffing all 32.
    const std::vector<RegChange>& last_changes() const { return last_changes_; }
    const core::StepOutcome& last_step() const { return last_step_; }

    /// The instruction that ran `back` steps ago, 0 being the most recent.
    /// Empty past the recorded horizon. The pipeline view rebuilds the stages
    /// behind the pc from this after a step it did not drive itself.
    std::optional<RecentStep> recent_step(std::size_t back) const {
        const StepDelta* delta = history_.recent(back);
        if (delta == nullptr) return std::nullopt;
        return RecentStep{delta->pc_before, delta->raw};
    }

    const std::string& uart_output() const { return hart_.bus().uart_output(); }

    u32 read_imem_word(Addr addr) const { return hart_.imem().read_word_raw(addr); }

    /// Fetch one instruction the way the hart does: a halfword first, then the
    /// upper half only if the low two bits say the instruction is 32 bits wide.
    /// Reading a plain word instead would return 0 for a compressed instruction
    /// sitting in the last halfword of imem, and would pull in the *next*
    /// instruction's bits for every other one.
    u32 read_imem_instr(Addr addr) const {
        const u32 low = hart_.imem().read_half_raw(addr);
        if (isa::instruction_length(low) == 2) return low;
        return low | (static_cast<u32>(hart_.imem().read_half_raw(addr + 2)) << 16);
    }

    /// How many bytes the instruction at `addr` occupies, for stepping a
    /// disassembly listing forward.
    u8 instr_length_at(Addr addr) const {
        return isa::instruction_length(hart_.imem().read_half_raw(addr));
    }
    u32 read_dmem_word(Addr addr) const { return hart_.bus().peek_word(addr); }

    /// Resolve a symbol name to its address, for `break main` and `x/16xw msg`.
    std::optional<Addr> lookup_symbol(std::string_view name) const;

    const SessionOptions& options() const { return options_; }
    SessionOptions& options() { return options_; }

private:
    /// One instruction, recording history and checking watchpoints.
    StopEvent step_instruction();
    StopEvent make_stop(StopReason reason) const;
    std::optional<StopEvent> check_stops();
    bool is_call(const core::StepOutcome& outcome) const;
    /// Run until control reaches `return_addr` with the stack unwound to at
    /// least `sp_floor`. The stack check is what stops recursion from ending
    /// the step at the wrong depth -- a heuristic, because nothing in the ISA
    /// marks a call frame.
    StopEvent run_until_return(Addr return_addr, u32 sp_floor);

    SessionOptions options_;
    core::Hart hart_;
    as::AssembledProgram program_;
    as::SourceFile source_;
    BreakpointSet breakpoints_;
    StepHistory history_;
    core::StepOutcome last_step_;
    std::vector<RegChange> last_changes_;
    bool loaded_ = false;
};

}  // namespace rv::dbg
