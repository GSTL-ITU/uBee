#include "dbg/session.hpp"

#include "isa/isa.hpp"

namespace rv::dbg {

const char* stop_reason_name(StopReason reason) {
    switch (reason) {
        case StopReason::Step: return "step";
        case StopReason::Breakpoint: return "breakpoint";
        case StopReason::Watchpoint: return "watchpoint";
        case StopReason::Halted: return "halted";
        case StopReason::Trap: return "trap";
        case StopReason::Watchdog: return "watchdog";
        case StopReason::NotRunning: return "not running";
        case StopReason::HistoryHorizon: return "history horizon";
    }
    return "?";
}

DebugSession::DebugSession(SessionOptions options)
    : options_(options), history_(options.history_capacity) {}

bool DebugSession::load_source(std::string text, std::string name,
                               const as::AssembleOptions& options) {
    source_ = as::SourceFile(std::move(name), std::move(text));
    program_ = as::assemble(source_, options);
    if (!program_.ok()) {
        loaded_ = false;
        return false;
    }
    loaded_ = true;
    hart_.imem().load_words(program_.imem_words);
    reset();
    hart_.bus().dmem().load_bytes(program_.dmem_bytes);
    // Breakpoints are keyed by line, so they survive a reassemble; this just
    // refreshes the addresses they resolve to.
    breakpoints_.rebind(program_.map);
    return true;
}

void DebugSession::load_image(const std::vector<Word>& imem, const std::vector<u8>& dmem) {
    program_ = as::AssembledProgram{};
    source_ = as::SourceFile();
    loaded_ = true;
    hart_.imem().load_words(imem);
    reset();
    if (!dmem.empty()) hart_.bus().dmem().load_bytes(dmem);
}

void DebugSession::reset() {
    hart_.reset(program_.entry);
    hart_.bus().dmem().load_bytes(program_.dmem_bytes);
    history_.clear();
    last_step_ = core::StepOutcome{};
    last_changes_.clear();
}

std::optional<u32> DebugSession::current_line() const {
    return program_.map.line_of(hart_.cpu().pc);
}

std::optional<std::pair<u8, u8>> DebugSession::current_slot() const {
    if (const auto entry = program_.map.at(hart_.cpu().pc)) {
        return std::make_pair(entry->slot, entry->count);
    }
    return std::nullopt;
}

std::optional<Addr> DebugSession::lookup_symbol(std::string_view name) const {
    if (const as::Symbol* symbol = program_.symbols.find(name)) return symbol->value;
    return std::nullopt;
}

StopEvent DebugSession::make_stop(StopReason reason) const {
    StopEvent event;
    event.reason = reason;
    event.pc = hart_.cpu().pc;
    event.line = program_.map.line_of(event.pc);
    event.halt = hart_.halt_reason();
    return event;
}

bool DebugSession::is_call(const core::StepOutcome& outcome) const {
    // A jump that keeps its link register is a call; one that discards it is a
    // plain jump. That is the only signal available, and it is the right one.
    if (!outcome.instr.valid()) return false;
    switch (outcome.instr.id) {
        case isa::InstrId::JAL:
        case isa::InstrId::JALR:
            return outcome.instr.rd != 0;
        // c.jal and c.jalr link to ra implicitly, so there is no rd field to
        // test -- they are always calls. c.j and c.jr link to x0 and never are.
        case isa::InstrId::C_JAL:
        case isa::InstrId::C_JALR:
            return true;
        default:
            return false;
    }
}

StopEvent DebugSession::step_instruction() {
    if (!loaded_) return make_stop(StopReason::NotRunning);
    if (hart_.halted()) return make_stop(StopReason::Halted);

    const core::StepOutcome outcome = hart_.step();
    history_.record(outcome, hart_);
    last_step_ = outcome;

    last_changes_.clear();
    if (outcome.reg_written != core::kNoReg) {
        last_changes_.push_back(
            RegChange{outcome.reg_written, outcome.reg_old, hart_.cpu().x[outcome.reg_written]});
    }

    if (auto stop = check_stops()) return *stop;
    return make_stop(StopReason::Step);
}

std::optional<StopEvent> DebugSession::check_stops() {
    if (hart_.halted()) {
        StopEvent event = make_stop(StopReason::Halted);
        event.cause = last_step_.cause;
        return event;
    }

    for (Watchpoint& watchpoint : breakpoints_.watchpoints()) {
        if (!watchpoint.enabled) continue;
        const u32 value = hart_.bus().peek_word(watchpoint.addr);
        if (value == watchpoint.last_value) continue;
        StopEvent event = make_stop(StopReason::Watchpoint);
        event.watch_addr = watchpoint.addr;
        event.watch_old = watchpoint.last_value;
        event.watch_new = value;
        watchpoint.last_value = value;
        return event;
    }

    if (const Breakpoint* breakpoint = breakpoints_.at(hart_.cpu().pc)) {
        StopEvent event = make_stop(StopReason::Breakpoint);
        event.breakpoint_id = breakpoint->id;
        if (Breakpoint* mutable_breakpoint = breakpoints_.find(breakpoint->id)) {
            ++mutable_breakpoint->hit_count;
            if (mutable_breakpoint->temporary) breakpoints_.remove(mutable_breakpoint->id);
        }
        return event;
    }

    return std::nullopt;
}

StopEvent DebugSession::run_until_return(Addr return_addr, u32 sp_floor) {
    for (u64 executed = 0; executed < options_.watchdog; ++executed) {
        StopEvent event = step_instruction();
        if (event.reason != StopReason::Step) return event;
        if (hart_.cpu().pc == return_addr && hart_.cpu().x[2] >= sp_floor) {
            return make_stop(StopReason::Step);
        }
    }
    return make_stop(StopReason::Watchdog);
}

StopEvent DebugSession::step(StepKind kind) {
    if (!loaded_) return make_stop(StopReason::NotRunning);
    if (hart_.halted()) return make_stop(StopReason::Halted);

    if (kind == StepKind::Instruction) return step_instruction();

    if (kind == StepKind::Out) {
        // Run until we come back to wherever ra currently points.
        return run_until_return(hart_.cpu().x[1], hart_.cpu().x[2]);
    }

    const std::optional<u32> start_line = current_line();
    const u32 sp_at_start = hart_.cpu().x[2];

    for (u64 executed = 0; executed < options_.watchdog; ++executed) {
        StopEvent event = step_instruction();
        if (event.reason != StopReason::Step) return event;

        if (kind == StepKind::LineOver && is_call(last_step_)) {
            // Past the call, which is 2 bytes if it was a compressed one.
            // run_until_return compares the pc for exact equality, so an
            // off-by-two here does not fail loudly -- it runs to the watchdog.
            const Addr return_addr = last_step_.pc_before + last_step_.instr.length;
            StopEvent inner = run_until_return(return_addr, sp_at_start);
            if (inner.reason != StopReason::Step) return inner;
        }

        // Landing rule for line stepping: the mapped line must have changed,
        // *and* the pc must be at the first word of it. Without the second
        // condition, `li a0, 0x12345` would stop halfway through itself.
        const std::optional<u32> now_line = program_.map.line_of(hart_.cpu().pc);
        if (!now_line.has_value()) return make_stop(StopReason::Step);
        if (now_line != start_line && program_.map.is_line_start(hart_.cpu().pc)) {
            return make_stop(StopReason::Step);
        }
    }

    return make_stop(StopReason::Watchdog);
}

std::optional<StopEvent> DebugSession::run_chunk(u64 budget) {
    if (!loaded_) return make_stop(StopReason::NotRunning);
    if (hart_.halted()) return make_stop(StopReason::Halted);

    for (u64 executed = 0; executed < budget; ++executed) {
        StopEvent event = step_instruction();
        if (event.reason != StopReason::Step) return event;
    }
    return std::nullopt;
}

StopEvent DebugSession::run() {
    if (!loaded_) return make_stop(StopReason::NotRunning);
    u64 executed = 0;
    while (executed < options_.watchdog) {
        const u64 slice = std::min<u64>(65536, options_.watchdog - executed);
        if (auto event = run_chunk(slice)) return *event;
        executed += slice;
    }
    return make_stop(StopReason::Watchdog);
}

StopEvent DebugSession::run_to(Addr addr) {
    if (!loaded_) return make_stop(StopReason::NotRunning);
    for (u64 executed = 0; executed < options_.watchdog; ++executed) {
        StopEvent event = step_instruction();
        if (event.reason != StopReason::Step) return event;
        if (hart_.cpu().pc == addr) return make_stop(StopReason::Step);
    }
    return make_stop(StopReason::Watchdog);
}

StopEvent DebugSession::reverse_step(StepKind kind) {
    if (!history_.can_undo()) return make_stop(StopReason::HistoryHorizon);

    const std::optional<u32> start_line = current_line();
    if (!history_.undo(hart_)) return make_stop(StopReason::HistoryHorizon);

    if (kind != StepKind::Instruction) {
        // Symmetric with forward line stepping: keep popping until the mapped
        // line changes and we are at the first word of it.
        while (history_.can_undo()) {
            const std::optional<u32> now_line = current_line();
            if (now_line != start_line && program_.map.is_line_start(hart_.cpu().pc)) break;
            if (!history_.undo(hart_)) break;
        }
    }

    last_changes_.clear();
    last_step_ = core::StepOutcome{};
    return make_stop(StopReason::Step);
}

}  // namespace rv::dbg
