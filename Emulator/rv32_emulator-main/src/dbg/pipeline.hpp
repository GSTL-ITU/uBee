// The five-stage pipeline, as a picture.
//
// The machine underneath is not pipelined, and this does not make it one:
// Hart::step() still fetches, decodes, executes and commits one instruction
// atomically, which is what keeps the emulator functionally true. What this
// adds is the view the hardware it describes actually presents -- five
// instructions in flight, one entering every cycle -- with every architectural
// effect landing the cycle its instruction is in EX.
//
// So IF and ID hold instructions that have been fetched and decoded but have
// not run, and MEM and WB hold instructions that finished running the cycle
// they left EX. Reading ahead is free: fetching and decoding here are pure
// reads of instruction memory, and nothing before EX touches the machine.
//
// Deliberately absent: hazards, stalls, forwarding, branch prediction. The
// front-end fetches straight through a branch, and when the branch turns out
// to go elsewhere the two instructions behind it are discarded. That is the
// honest minimum needed to draw the picture at all; the rest is a later
// conversation.
#pragma once

#include <array>
#include <cstddef>
#include <deque>
#include <string_view>

#include "dbg/session.hpp"
#include "isa/decode.hpp"

namespace rv::dbg {

enum class Stage : u8 { IF, ID, EX, MEM, WB };
inline constexpr std::size_t kNumStages = 5;

/// "IF", "ID", "EX", "MEM", "WB".
const char* stage_name(Stage stage);
/// The same, clipped to two columns so a space-time diagram lines up.
const char* stage_abbrev(Stage stage);

/// One instruction sitting in one stage. Everything before EX is speculative:
/// fetched and decoded, but it has not touched the machine.
struct Slot {
    bool valid = false;
    Addr pc = 0;
    Word raw = 0;
    isa::DecodedInstr instr{};
    /// Fetch order, and the identity of the instruction across stages. Zero
    /// means there is nothing here -- a bubble.
    u64 serial = 0;
    bool committed = false;
    /// Whether committing this instruction sent the pc somewhere other than
    /// the next address. Only meaningful once `committed`, and only
    /// interesting for a branch or a jump -- for anything else a redirect
    /// means a trap.
    bool redirected = false;

    /// Set while this instruction was in EX, if the front-end was sent to its
    /// target on the strength of the always-taken guess. `predicted_pc` is
    /// where it was sent, and write-back checks the guess against what really
    /// happened.
    bool predicted = false;
    Addr predicted_pc = 0;
};

/// What one cycle looked like. Kept both to draw the space-time diagram and to
/// undo the cycle: putting these slots back is all reverse_tick() has to do
/// beyond handing the instruction itself back to the session.
struct CycleRecord {
    u64 tick = 0;
    // Not `slots`: Qt defines that as a macro, and this header is included
    // from the widget that draws it.
    std::array<Slot, kNumStages> stages{};
    Addr fetch_pc = 0;
    bool committed = false;
    u8 flushed = 0;
    bool mispredicted = false;
};

class Pipeline {
public:
    explicit Pipeline(DebugSession& session);

    /// Advance exactly one cycle. The event returned is whatever the session
    /// reported for the instruction that reached EX; a cycle that only moved
    /// bubbles along reports a plain Step.
    StopEvent tick();

    /// Undo one cycle, including the instruction it committed. False when the
    /// recording runs out.
    bool reverse_tick();
    bool can_reverse() const { return history_.size() > 1; }

    /// Adopt wherever the machine now is, after something other than tick()
    /// moved it -- a source-line step, a run, a reverse step. The instruction
    /// that just ran is put back in EX and the next two are fetched behind it,
    /// so the pc rule below still holds on the next tick.
    void resync();

    /// Empty the pipe. After this the first instruction takes three ticks to
    /// commit, which is the thing worth seeing.
    void reset();

    const Slot& at(Stage stage) const { return slots_[static_cast<std::size_t>(stage)]; }

    /// The instruction's own spelling -- `c.addi`, not the `addi` it expands
    /// to. Empty for a bubble, "??" for something that does not decode.
    std::string_view mnemonic(Stage stage) const;

    /// Cycles run since the pipe was last filled from scratch. Emphatically
    /// *not* cpu().cycle: that is architectural time, it drives the timer and
    /// the cycle CSRs, and a cycle spent on a bubble is not one the machine
    /// experienced.
    u64 ticks() const { return ticks_; }
    bool drained() const;
    /// Cells discarded by the most recent redirect, whichever kind.
    u8 last_flushed() const { return last_flushed_; }
    /// True when the most recent cycle found the always-taken guess wrong.
    bool last_mispredicted() const { return last_mispredicted_; }

    /// Oldest cycle first. The first entry is the state the pipe started in and
    /// carries tick 0.
    const std::deque<CycleRecord>& history() const { return history_; }

    static constexpr std::size_t kHistoryDepth = 4096;

private:
    Slot fetch(Addr addr);
    /// Send the front-end to the target of the instruction in EX, if it has
    /// one. Returns how many fetched cells that threw away.
    u8 predict_taken();
    void record(bool committed, u8 flushed);
    void start_over();
    /// A cycle in which nothing reached EX still has to say where the
    /// machine is, because the caller reports it the same way.
    StopEvent make_step_event() const;

    DebugSession* session_;
    std::array<Slot, kNumStages> slots_{};
    Addr fetch_pc_ = 0;
    u64 ticks_ = 0;
    u64 next_serial_ = 1;
    u8 last_flushed_ = 0;
    bool last_mispredicted_ = false;
    std::deque<CycleRecord> history_;
};

}  // namespace rv::dbg
