#include "dbg/pipeline.hpp"

namespace rv::dbg {
namespace {

constexpr std::size_t index_of(Stage stage) { return static_cast<std::size_t>(stage); }

/// Whether the target is knowable from the encoding alone -- the pc plus the
/// immediate. That is every branch and every pc-relative jump. `jalr` and the
/// compressed register jumps are not: their target is in a register, and a
/// register is not read until the instruction runs.
bool has_static_target(const isa::DecodedInstr& instr) {
    if (!instr.valid()) return false;
    switch (instr.desc().syntax) {
        case isa::OperandSyntax::RS1_RS2_LBL:   // beq, bne, blt, bge, bltu, bgeu
        case isa::OperandSyntax::RD_LBL:        // jal
        case isa::OperandSyntax::C_LBL:         // c.j, c.jal
        case isa::OperandSyntax::C_RS1P_LBL:    // c.beqz, c.bnez
            return true;
        default:
            return false;
    }
}

}  // namespace

const char* stage_name(Stage stage) {
    switch (stage) {
        case Stage::IF: return "IF";
        case Stage::ID: return "ID";
        case Stage::EX: return "EX";
        case Stage::MEM: return "MEM";
        case Stage::WB: return "WB";
    }
    return "??";
}

const char* stage_abbrev(Stage stage) {
    // Two columns each, so the space-time diagram is a grid rather than a
    // ragged edge. Only MEM has to give anything up.
    return stage == Stage::MEM ? "ME" : stage_name(stage);
}

Pipeline::Pipeline(DebugSession& session) : session_(&session) { reset(); }

// ---------------------------------------------------------------------------
// Filling and emptying
// ---------------------------------------------------------------------------

void Pipeline::start_over() {
    slots_ = {};
    history_.clear();
    ticks_ = 0;
    last_flushed_ = 0;
    last_mispredicted_ = false;
}

void Pipeline::reset() {
    start_over();
    fetch_pc_ = session_->cpu().pc;
    record(false, 0);
}

void Pipeline::resync() {
    start_over();

    // Put back what the machine really has. Only one instruction has finished
    // -- the one that just wrote back -- and behind it a real pipe would be
    // full of the work already fetched, with the instruction at the pc four
    // stages from retiring. Recovering that is what stops the later stages
    // sitting empty for anyone who never presses Step Cycle.
    if (const std::optional<RecentStep> ran = session_->recent_step(0); ran.has_value()) {
        Slot& retired = slots_[index_of(Stage::WB)];
        retired.valid = true;
        retired.pc = ran->pc;
        retired.raw = ran->raw;
        retired.instr = isa::decode(ran->raw);
        retired.serial = next_serial_++;
        retired.committed = true;
        retired.redirected = session_->cpu().pc != retired.pc + retired.instr.length;
    }

    fetch_pc_ = session_->cpu().pc;
    if (session_->has_program() && !session_->halted()) {
        // Nearest to retiring first, so the instruction at the pc lands in MEM
        // and reaches write-back on the very next cycle.
        for (const Stage stage : {Stage::MEM, Stage::EX}) {
            Slot& slot = slots_[index_of(stage)];
            slot = fetch(fetch_pc_);
            fetch_pc_ += slot.instr.length;
        }
        // Whatever is in EX has already had its target computed by the time the
        // pipe looks like this, so the two behind it come from there.
        predict_taken();
        for (const Stage stage : {Stage::ID, Stage::IF}) {
            Slot& slot = slots_[index_of(stage)];
            slot = fetch(fetch_pc_);
            fetch_pc_ += slot.instr.length;
        }
    }
    record(false, 0);
}

Slot Pipeline::fetch(Addr addr) {
    Slot slot;
    slot.valid = true;
    slot.pc = addr;
    slot.raw = session_->read_imem_instr(addr);
    slot.instr = isa::decode(slot.raw);
    slot.serial = next_serial_++;
    return slot;
}

u8 Pipeline::predict_taken() {
    Slot& executing = slots_[index_of(Stage::EX)];
    if (!executing.valid || executing.predicted) return 0;
    if (!has_static_target(executing.instr)) return 0;

    // EX is where the target adder lives, so this is where the front-end learns
    // there is somewhere else to go. Which way the branch actually goes is not
    // known until it runs -- so guess, and always guess taken. There is no
    // history to consult, and an unconditional jump is right every time.
    executing.predicted = true;
    executing.predicted_pc = executing.pc + static_cast<u32>(executing.instr.imm);

    // What the front-end fetched while this was still being decoded came from
    // straight ahead, and the guess says otherwise.
    u8 discarded = 0;
    for (const Stage stage : {Stage::ID, Stage::IF}) {
        if (slots_[index_of(stage)].valid) ++discarded;
        slots_[index_of(stage)] = Slot{};
    }
    fetch_pc_ = executing.predicted_pc;
    return discarded;
}

void Pipeline::record(bool committed, u8 flushed) {
    CycleRecord entry;
    entry.tick = ticks_;
    entry.stages = slots_;
    entry.fetch_pc = fetch_pc_;
    entry.committed = committed;
    entry.flushed = flushed;
    entry.mispredicted = last_mispredicted_;
    history_.push_back(entry);
    if (history_.size() > kHistoryDepth) history_.pop_front();
}

// ---------------------------------------------------------------------------
// One cycle
// ---------------------------------------------------------------------------

StopEvent Pipeline::tick() {
    // Shift, back half first so nothing is overwritten before it has moved.
    slots_[index_of(Stage::WB)] = slots_[index_of(Stage::MEM)];
    slots_[index_of(Stage::MEM)] = slots_[index_of(Stage::EX)];
    slots_[index_of(Stage::EX)] = slots_[index_of(Stage::ID)];
    slots_[index_of(Stage::ID)] = slots_[index_of(Stage::IF)];
    slots_[index_of(Stage::IF)] = Slot{};

    StopEvent event;
    u8 flushed = 0;
    bool committed = false;
    bool mispredicted = false;

    // Write-back is where an instruction becomes part of the machine: the
    // register file is written here, and this emulator does the whole
    // instruction in one go, so this is the cycle it is run.
    //
    // A halted machine still drains: what is already in flight keeps moving
    // along, it simply has nothing following it.
    Slot& retiring = slots_[index_of(Stage::WB)];
    const bool runnable = retiring.valid && session_->has_program() && !session_->halted();

    if (runnable && retiring.pc != session_->cpu().pc) {
        // The machine moved without going through here. Rather than run an
        // instruction the pc has left behind, adopt where it actually is.
        resync();
        return make_step_event();
    }

    if (runnable) {
        const Addr sequential = retiring.pc + retiring.instr.length;
        // Where the front-end was told to go: the guess if one was made in EX,
        // otherwise straight ahead.
        const Addr expected = retiring.predicted ? retiring.predicted_pc : sequential;

        event = session_->step(StepKind::Instruction);
        committed = true;
        retiring.committed = true;
        retiring.redirected = session_->cpu().pc != sequential;

        if (session_->cpu().pc != expected) {
            // The guess was wrong, or something nobody guesses about happened:
            // a jalr, a trap, an interrupt. Either way everything in the pipe
            // came from the wrong place.
            mispredicted = retiring.predicted;
            for (const Stage stage : {Stage::MEM, Stage::EX, Stage::ID, Stage::IF}) {
                if (slots_[index_of(stage)].valid) ++flushed;
                slots_[index_of(stage)] = Slot{};
            }
            fetch_pc_ = session_->cpu().pc;
        }
    }

    // EX computes the branch target, so this is where the front-end turns.
    flushed = static_cast<u8>(flushed + predict_taken());

    // Fetch, unless the machine has stopped for good. A breakpoint still
    // fetches: the instruction it guards belongs in the pipe, fetched and not
    // run, which is exactly where the machine is standing.
    if (session_->has_program() && !session_->halted()) {
        Slot& fetched = slots_[index_of(Stage::IF)];
        fetched = fetch(fetch_pc_);
        fetch_pc_ += fetched.instr.length;
    }

    ++ticks_;
    last_flushed_ = flushed;
    last_mispredicted_ = mispredicted;
    record(committed, flushed);
    return committed ? event : make_step_event();
}

bool Pipeline::reverse_tick() {
    // The first record is where the pipe started, not a cycle that ran.
    if (history_.size() < 2) return false;
    if (history_.back().committed && !session_->can_reverse()) return false;

    const bool undo_instruction = history_.back().committed;
    history_.pop_back();
    if (undo_instruction) session_->reverse_step(StepKind::Instruction);

    const CycleRecord& now = history_.back();
    slots_ = now.stages;
    fetch_pc_ = now.fetch_pc;
    ticks_ = now.tick;
    last_flushed_ = now.flushed;
    last_mispredicted_ = now.mispredicted;
    return true;
}

// ---------------------------------------------------------------------------
// Inspection
// ---------------------------------------------------------------------------

std::string_view Pipeline::mnemonic(Stage stage) const {
    const Slot& slot = at(stage);
    if (!slot.valid) return {};
    if (!slot.instr.valid()) return "??";
    return slot.instr.desc().mnemonic;
}

bool Pipeline::drained() const {
    for (const Slot& slot : slots_) {
        if (slot.valid) return false;
    }
    return true;
}

StopEvent Pipeline::make_step_event() const {
    StopEvent event;
    event.reason = StopReason::Step;
    event.pc = session_->cpu().pc;
    event.line = session_->current_line();
    return event;
}

}  // namespace rv::dbg
