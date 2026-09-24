#include "dbg/breakpoints.hpp"

#include <algorithm>

namespace rv::dbg {

void BreakpointSet::reindex() {
    by_addr_.clear();
    for (std::size_t i = 0; i < breakpoints_.size(); ++i) {
        if (breakpoints_[i].enabled) by_addr_[breakpoints_[i].addr] = i;
    }
}

const Breakpoint* BreakpointSet::add_line(u32 line, const as::SourceMap& map, bool temporary) {
    const auto landed = map.next_line_with_code(line);
    if (!landed) return nullptr;
    const std::vector<Addr>& addrs = map.addrs_for_line(*landed);
    if (addrs.empty()) return nullptr;

    Breakpoint breakpoint;
    breakpoint.id = next_id_++;
    breakpoint.line = *landed;
    breakpoint.requested_line = line;
    breakpoint.snapped = *landed != line;
    breakpoint.addr = addrs.front();
    breakpoint.temporary = temporary;
    breakpoints_.push_back(breakpoint);
    reindex();
    return &breakpoints_.back();
}

const Breakpoint* BreakpointSet::add_addr(Addr addr, const as::SourceMap& map) {
    Breakpoint breakpoint;
    breakpoint.id = next_id_++;
    breakpoint.addr = addr;
    if (const auto line = map.line_of(addr)) {
        breakpoint.line = *line;
        breakpoint.requested_line = *line;
    }
    breakpoints_.push_back(breakpoint);
    reindex();
    return &breakpoints_.back();
}

const Watchpoint* BreakpointSet::add_watch(Addr addr, u32 current_value) {
    Watchpoint watchpoint;
    watchpoint.id = next_id_++;
    watchpoint.addr = addr;
    watchpoint.last_value = current_value;
    watchpoints_.push_back(watchpoint);
    return &watchpoints_.back();
}

bool BreakpointSet::remove(u32 id) {
    const auto breakpoint = std::find_if(breakpoints_.begin(), breakpoints_.end(),
                                         [id](const Breakpoint& b) { return b.id == id; });
    if (breakpoint != breakpoints_.end()) {
        breakpoints_.erase(breakpoint);
        reindex();
        return true;
    }
    const auto watchpoint = std::find_if(watchpoints_.begin(), watchpoints_.end(),
                                         [id](const Watchpoint& w) { return w.id == id; });
    if (watchpoint != watchpoints_.end()) {
        watchpoints_.erase(watchpoint);
        return true;
    }
    return false;
}

bool BreakpointSet::set_enabled(u32 id, bool enabled) {
    if (Breakpoint* breakpoint = find(id)) {
        breakpoint->enabled = enabled;
        reindex();
        return true;
    }
    for (Watchpoint& watchpoint : watchpoints_) {
        if (watchpoint.id != id) continue;
        watchpoint.enabled = enabled;
        return true;
    }
    return false;
}

void BreakpointSet::clear() {
    breakpoints_.clear();
    watchpoints_.clear();
    by_addr_.clear();
}

void BreakpointSet::rebind(const as::SourceMap& map) {
    for (Breakpoint& breakpoint : breakpoints_) {
        const std::vector<Addr>& addrs = map.addrs_for_line(breakpoint.line);
        if (addrs.empty()) {
            // The line no longer emits code -- it was deleted or commented
            // out. Disable rather than drop, so the user can see what happened
            // instead of silently losing a breakpoint.
            breakpoint.enabled = false;
            continue;
        }
        breakpoint.addr = addrs.front();
    }
    reindex();
}

const Breakpoint* BreakpointSet::at(Addr addr) const {
    const auto it = by_addr_.find(addr);
    return it == by_addr_.end() ? nullptr : &breakpoints_[it->second];
}

Breakpoint* BreakpointSet::find(u32 id) {
    const auto it = std::find_if(breakpoints_.begin(), breakpoints_.end(),
                                 [id](const Breakpoint& b) { return b.id == id; });
    return it == breakpoints_.end() ? nullptr : &*it;
}

bool BreakpointSet::has_breakpoint_on_line(u32 line) const {
    return std::any_of(breakpoints_.begin(), breakpoints_.end(),
                       [line](const Breakpoint& b) { return b.line == line && b.enabled; });
}

bool BreakpointSet::toggle_line(u32 line, const as::SourceMap& map) {
    const auto landed = map.next_line_with_code(line);
    const u32 target = landed.value_or(line);
    const auto existing = std::find_if(breakpoints_.begin(), breakpoints_.end(),
                                       [target](const Breakpoint& b) { return b.line == target; });
    if (existing != breakpoints_.end()) {
        breakpoints_.erase(existing);
        reindex();
        return false;
    }
    return add_line(line, map) != nullptr;
}

}  // namespace rv::dbg
