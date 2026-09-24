// Architectural state of the hart.
#pragma once

#include <array>

#include "core/csrfile.hpp"
#include "isa/types.hpp"

namespace rv::core {

/// "No register" / "no address" sentinels, used by ExecResult and later by the
/// reverse-step delta to say that an instruction touched nothing.
inline constexpr RegIdx kNoReg = 0xff;
inline constexpr Addr kNoAddr = 0xffff'ffffu;

struct CpuState {
    Addr pc = 0;
    std::array<u32, kNumRegs> x{};
    CsrFile csr;
    u64 cycle = 0;
    u64 instret = 0;

    u32 reg(RegIdx index) const { return x[index]; }

    /// Every register writeback goes through here, which is the one place x0
    /// stays hardwired to zero. Bypassing it is the classic source of "why is
    /// zero suddenly 42".
    void set_reg(RegIdx index, u32 value) {
        if (index != 0) x[index] = value;
    }

    void reset(Addr entry) {
        pc = entry;
        x.fill(0);
        csr.reset();
        cycle = 0;
        instret = 0;
    }
};

}  // namespace rv::core
