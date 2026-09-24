// The bidirectional map between source lines and instruction addresses.
//
// This is what "step one line" is built on, and it is emitted by the assembler
// as it encodes rather than reconstructed afterwards -- retrofitting it into a
// finished assembler would mean touching every emit site.
//
// The relationship is one-to-many: `li a0, 0x12345` is one source line and two
// instructions. `slot`/`count` record that honestly, so the debugger can keep
// the execution arrow on the line while showing [1/2] and [2/2], and the
// disassembly view can indent the continuation. A learner then *sees* that one
// line became two instructions, which is exactly the thing worth teaching.
#pragma once

#include <optional>
#include <vector>

#include "asm/pseudo.hpp"
#include "isa/types.hpp"

namespace rv::as {

struct AddrEntry {
    Addr addr = 0;
    u32 line = 0;
    u8 slot = 0;   // which word of this line, 0-based
    u8 count = 1;  // how many words the line emitted in total
    PseudoId pseudo = kInvalidPseudo;
};

class SourceMap {
public:
    void add(AddrEntry entry);
    /// Sort by address and build the line index. Call once, after assembly.
    void finalize();
    void clear();

    std::optional<AddrEntry> at(Addr addr) const;
    std::optional<u32> line_of(Addr addr) const;

    /// Addresses emitted by one source line, in order. Indexed lookup because
    /// the editor's gutter asks this for every visible line on every redraw.
    const std::vector<Addr>& addrs_for_line(u32 line) const;

    /// True when `addr` is the first word of its source line. Stepping by line
    /// must land only on these, or it stops in the middle of a `li`.
    bool is_line_start(Addr addr) const;

    /// The first line at or after `line` that emitted code, for snapping a
    /// breakpoint set on a blank line or a comment.
    std::optional<u32> next_line_with_code(u32 line) const;

    std::optional<Addr> first_addr_at_or_after(u32 line) const;

    bool empty() const { return by_addr_.empty(); }
    const std::vector<AddrEntry>& entries() const { return by_addr_; }

private:
    std::vector<AddrEntry> by_addr_;              // sorted, binary searched
    std::vector<std::vector<Addr>> line_to_addrs_;  // indexed by line - 1
};

}  // namespace rv::as
