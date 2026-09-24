// A flat, byte-addressable memory region with alignment and bounds checking.
//
// Two instances exist per machine and they are genuinely separate address
// spaces: IMEM (fetched from, never loaded from) and DMEM (loaded/stored,
// never fetched from). That is what "Harvard" means here, and it is why
// `la a0, some_data_label` cannot use auipc -- see docs/memory-map.md.
#pragma once

#include <vector>

#include "core/trap.hpp"
#include "isa/types.hpp"

namespace rv::core {

/// Result of a memory access. `ok == false` means the access trapped and
/// `cause` says why; `value` is meaningful only for successful reads.
struct MemResult {
    u32 value = 0;
    bool ok = true;
    TrapCause cause = TrapCause::None;

    static MemResult success(u32 value) { return MemResult{value, true, TrapCause::None}; }
    static MemResult failure(TrapCause cause) { return MemResult{0, false, cause}; }
};

class Memory {
public:
    Memory(Addr base, u32 size);

    Addr base() const { return base_; }
    u32 size() const { return size_; }
    bool contains(Addr addr) const { return addr >= base_ && addr - base_ < size_; }

    /// Architectural read. `width` is 1, 2 or 4; `is_signed` selects lb/lh
    /// versus lbu/lhu behaviour. Misaligned and out-of-range accesses trap.
    MemResult read(Addr addr, u8 width, bool is_signed) const;

    /// Architectural write. Misaligned and out-of-range accesses trap.
    MemResult write(Addr addr, u8 width, u32 value);

    // Debugger-facing accessors: never trap, never fault. Out-of-range reads
    // return 0 and out-of-range writes are dropped, so a memory viewer can
    // scroll past the end of RAM without special-casing every call site.
    u32 read_word_raw(Addr addr) const;
    /// Halfword read, for fetching a compressed instruction and for assembling
    /// a 32-bit one that straddles a word boundary.
    u16 read_half_raw(Addr addr) const;
    void write_word_raw(Addr addr, u32 value);
    u8 read_byte_raw(Addr addr) const;

    /// Load a word image at `base()`, e.g. from a .mem file or an assembler.
    void load_words(const std::vector<u32>& words);
    /// Load a byte image at `base()`.
    void load_bytes(const std::vector<u8>& bytes);

    void clear();

    /// Change how much there is. Contents that still fit are kept, so growing
    /// a memory does not throw away the program or the data already in it.
    void resize(u32 size);

    /// Highest byte address written since the last clear(), relative to base.
    /// Used to decide how many lines a .mem export needs.
    u32 high_water() const { return high_water_; }

private:
    std::vector<u8> bytes_;
    Addr base_;
    u32 size_;
    u32 high_water_ = 0;
};

}  // namespace rv::core
