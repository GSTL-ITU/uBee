// Step history, for reverse stepping.
//
// RV32 makes this genuinely cheap: an instruction writes at most one register,
// at most one CSR and at most one memory word, so a fixed-size record is a
// complete inverse. A 64 Ki ring of records this size is a few megabytes and
// covers far more steps than any teaching scenario needs.
//
// Being able to say "wait, what did that srai do?" and press a key to watch it
// again is, per unit of code, the most valuable thing in the debugger.
#pragma once

#include <vector>

#include "core/csrfile.hpp"
#include "core/hart.hpp"
#include "isa/types.hpp"

namespace rv::dbg {

struct StepDelta {
    Addr pc_before = 0;
    Word raw = 0;

    RegIdx reg_index = core::kNoReg;
    u32 reg_before = 0;

    CsrAddr csr_index = 0;
    bool wrote_csr = false;
    u32 csr_before = 0;

    Addr mem_addr = core::kNoAddr;
    u32 mem_before = 0;
    u8 mem_width = 0;

    u64 cycle_before = 0;
    u64 instret_before = 0;
    /// The device a write landed on, and the state its peek/poke could not
    /// carry. See StepOutcome for why one field is enough.
    u32 device_slot = kNoDevice;
    u64 device_aux_before = 0;

    static constexpr u32 kNoDevice = 0xffff'ffffu;

    /// Index into the side table of full CSR snapshots, or kNoSnapshot. Traps
    /// and mret change several CSRs at once; rather than widening this record
    /// for a rare case, they store a whole copy.
    u32 csr_snapshot = kNoSnapshot;

    static constexpr u32 kNoSnapshot = 0xffff'ffffu;
};

class StepHistory {
public:
    explicit StepHistory(std::size_t capacity = 1u << 16);

    void record(const core::StepOutcome& outcome, const core::Hart& hart);

    /// Undo the most recent step. Returns false when the history is empty or
    /// the ring has wrapped past the step being asked for -- it never silently
    /// does something approximate.
    bool undo(core::Hart& hart);

    /// The `back`th most recent record, 0 being the step that just ran, or null
    /// past what is still kept. Read-only, and deliberately narrow: a view that
    /// needs to know what is behind the pc should not be able to move it.
    const StepDelta* recent(std::size_t back) const;

    bool can_undo() const { return depth_ > 0; }
    std::size_t depth() const { return depth_; }
    std::size_t capacity() const { return deltas_.size(); }
    /// True if the ring has wrapped, so the oldest steps are gone.
    bool at_horizon() const { return depth_ == deltas_.size(); }

    void clear();

private:
    std::vector<StepDelta> deltas_;
    std::vector<core::CsrFile> csr_snapshots_;
    std::size_t next_ = 0;   // where the next record goes
    std::size_t depth_ = 0;  // how many records can still be undone
};

}  // namespace rv::dbg
