#include "dbg/history.hpp"

namespace rv::dbg {

StepHistory::StepHistory(std::size_t capacity) : deltas_(capacity) {}

void StepHistory::record(const core::StepOutcome& outcome, const core::Hart& /*hart*/) {
    StepDelta delta;
    delta.pc_before = outcome.pc_before;
    delta.raw = outcome.raw;
    delta.reg_index = outcome.reg_written;
    delta.reg_before = outcome.reg_old;
    delta.csr_index = outcome.csr_written;
    delta.wrote_csr = outcome.wrote_csr;
    delta.csr_before = outcome.csr_old;
    delta.mem_addr = outcome.mem_written;
    delta.mem_before = outcome.mem_old;
    delta.mem_width = outcome.mem_width;
    delta.cycle_before = outcome.cycle_before;
    delta.instret_before = outcome.instret_before;
    delta.device_slot = outcome.device_slot == core::StepOutcome::kNoDevice
                            ? StepDelta::kNoDevice
                            : static_cast<u32>(outcome.device_slot);
    delta.device_aux_before = outcome.device_aux_before;

    if (outcome.touched_many_csrs) {
        // The pre-step copy, not the current one: the whole point is to undo
        // what the trap wrote. The table only grows during a run and is cleared
        // on reset, and traps are rare, so it stays small in practice.
        delta.csr_snapshot = static_cast<u32>(csr_snapshots_.size());
        csr_snapshots_.push_back(outcome.csr_before);
    }

    deltas_[next_] = delta;
    next_ = (next_ + 1) % deltas_.size();
    if (depth_ < deltas_.size()) ++depth_;
}

const StepDelta* StepHistory::recent(std::size_t back) const {
    if (back >= depth_) return nullptr;
    const std::size_t size = deltas_.size();
    return &deltas_[(next_ + size - 1 - back) % size];
}

bool StepHistory::undo(core::Hart& hart) {
    if (depth_ == 0) return false;

    next_ = (next_ + deltas_.size() - 1) % deltas_.size();
    --depth_;
    const StepDelta& delta = deltas_[next_];

    core::CpuState& cpu = hart.cpu();
    cpu.pc = delta.pc_before;
    if (delta.reg_index != core::kNoReg) cpu.x[delta.reg_index] = delta.reg_before;

    if (delta.csr_snapshot != StepDelta::kNoSnapshot) {
        cpu.csr = csr_snapshots_[delta.csr_snapshot];
        csr_snapshots_.resize(delta.csr_snapshot);
    } else if (delta.wrote_csr) {
        cpu.csr.raw_write(delta.csr_index, delta.csr_before);
    }

    if (delta.mem_addr != core::kNoAddr) {
        // The full pre-image word goes back, which is why sb and sh need no
        // special handling here.
        hart.bus().poke_word(delta.mem_addr, delta.mem_before);
    }

    if (delta.device_slot != StepDelta::kNoDevice) {
        if (core::Device* device = hart.bus().device_at_slot(delta.device_slot)) {
            device->set_aux_state(delta.device_aux_before);
        }
    }
    cpu.cycle = delta.cycle_before;
    cpu.instret = delta.instret_before;
    hart.bus().set_now(cpu.cycle);

    // Stepping back out of a halt makes the machine runnable again.
    hart.clear_halt();
    return true;
}

void StepHistory::clear() {
    csr_snapshots_.clear();
    next_ = 0;
    depth_ = 0;
}

}  // namespace rv::dbg
