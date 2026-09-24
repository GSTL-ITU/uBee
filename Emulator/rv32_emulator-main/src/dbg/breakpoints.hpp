// Breakpoints and watchpoints.
//
// Breakpoints are stored by *source line*, not by address. That is what lets
// them survive the edit-and-reassemble cycle which is the whole point of the
// embedded editor: having every breakpoint vanish on each F5 would make the
// tool infuriating to use.
#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "asm/sourcemap.hpp"
#include "isa/types.hpp"

namespace rv::dbg {

struct Breakpoint {
    u32 id = 0;
    u32 line = 0;
    Addr addr = 0;
    bool enabled = true;
    bool temporary = false;
    u32 hit_count = 0;
    /// True when the requested line had no code and the breakpoint moved
    /// forward to the next line that did. The UI shows it where it landed
    /// rather than where it was asked for.
    bool snapped = false;
    u32 requested_line = 0;
};

/// A data watchpoint: stop when this word changes.
struct Watchpoint {
    u32 id = 0;
    Addr addr = 0;
    u32 last_value = 0;
    bool enabled = true;
};

class BreakpointSet {
public:
    /// Set a breakpoint on a source line, snapping forward to the next line
    /// that emitted code. Returns nullptr if nothing at or after that line
    /// produced any.
    const Breakpoint* add_line(u32 line, const as::SourceMap& map, bool temporary = false);

    /// Set one directly on an address, for `break *0x24`.
    const Breakpoint* add_addr(Addr addr, const as::SourceMap& map);

    const Watchpoint* add_watch(Addr addr, u32 current_value);

    bool remove(u32 id);
    bool set_enabled(u32 id, bool enabled);
    void clear();

    /// Rebuild every breakpoint's address from its line after a reassemble.
    /// Breakpoints whose line no longer has code become disabled rather than
    /// disappearing, so the user can see what happened.
    void rebind(const as::SourceMap& map);

    const Breakpoint* at(Addr addr) const;
    Breakpoint* find(u32 id);

    const std::vector<Breakpoint>& breakpoints() const { return breakpoints_; }
    const std::vector<Watchpoint>& watchpoints() const { return watchpoints_; }
    std::vector<Watchpoint>& watchpoints() { return watchpoints_; }

    bool has_breakpoint_on_line(u32 line) const;
    /// Add if absent, remove if present. What the editor's F9 key does.
    bool toggle_line(u32 line, const as::SourceMap& map);

private:
    void reindex();

    std::vector<Breakpoint> breakpoints_;
    std::vector<Watchpoint> watchpoints_;
    std::unordered_map<Addr, std::size_t> by_addr_;
    u32 next_id_ = 1;
};

}  // namespace rv::dbg
