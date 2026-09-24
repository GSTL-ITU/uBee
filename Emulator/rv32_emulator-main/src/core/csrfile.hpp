// Storage for the machine-mode CSRs.
//
// The table of which CSRs exist lives in isa/csr.hpp; this is only the state.
#pragma once

#include <array>
#include <cstddef>

#include "isa/csr.hpp"
#include "isa/types.hpp"

namespace rv::core {

using isa::CsrId;
using isa::kCsrCount;

class CsrFile {
public:
    CsrFile() { reset(); }

    void reset();

    /// True if this CSR address is implemented. Accessing an unimplemented CSR
    /// raises IllegalInstruction.
    bool exists(CsrAddr addr) const { return isa::csr_index(addr) != kCsrCount; }

    /// Architectural read. Unimplemented addresses read as zero; callers check
    /// exists() first and raise the trap themselves.
    u32 read(CsrAddr addr) const;

    /// Architectural write, filtered through the CSR's WARL mask so that
    /// read-only and partially-writable registers behave correctly.
    void write(CsrAddr addr, u32 value);

    /// Unmasked access. Used by trap entry, which must write mepc/mcause even
    /// though they are architecturally constrained, and by reverse-step undo.
    void raw_write(CsrAddr addr, u32 value);

    /// Refresh the counter CSRs from the hart's counters, so that a csrr of
    /// `cycle` observes the cycle it is executing in.
    void set_counters(u64 cycle, u64 instret);

    /// Recompose the writable machine counters from their two halves. The hart
    /// reads these back after an instruction writes one, so that `csrw mcycle`
    /// actually moves the counter instead of being clobbered by the next
    /// set_counters(). The user-mode shadows at 0xc00 have no such pair: they
    /// are read-only, and writing one raises IllegalInstruction.
    u64 machine_cycle() const;
    u64 machine_instret() const;

    /// Publish the interrupt lines the devices are asserting. mip is read-only
    /// to software and computed from device state, so it is pushed in here
    /// rather than being written by a csrrw.
    void set_pending_interrupts(u32 mip);

    // Index-based access for the reverse-step delta, which stores a table
    // index rather than an address.
    u32 by_index(std::size_t index) const { return values_[index]; }
    void set_by_index(std::size_t index, u32 value) { values_[index] = value; }

private:
    std::array<u32, kCsrCount> values_{};
};

}  // namespace rv::core
