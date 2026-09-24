// The CSR table: which control and status registers exist, what they are
// called, and which of their bits are writable.
//
// This is ISA knowledge (the Zicsr extension), so it lives in rv_isa alongside
// the instruction table -- the disassembler needs CSR names, and it must not
// depend on the machine implementation to get them. The storage itself is
// machine state and lives in core::CsrFile.
#pragma once

#include <array>
#include <cstddef>
#include <string_view>

#include "isa/types.hpp"

namespace rv::isa {

enum class CsrId : u16 {
#define RV_CSR(id, ...) id,
#include "isa/csr_table.def"
#undef RV_CSR
    Count
};

inline constexpr std::size_t kCsrCount = static_cast<std::size_t>(CsrId::Count);

struct CsrDesc {
    std::string_view name;
    CsrAddr addr;
    u32 reset;
    /// WARL mask: bits set to 0 ignore writes and read back as the reset value.
    /// A mask of 0 makes the CSR read-only.
    u32 write_mask;
};

inline constexpr std::array<CsrDesc, kCsrCount> kCsrTable = {{
#define RV_CSR(id, name, addr, reset, mask) CsrDesc{name, (addr), (reset), (mask)},
#include "isa/csr_table.def"
#undef RV_CSR
}};

/// Table index for a CSR address, or kCsrCount if the CSR is not implemented.
std::size_t csr_index(CsrAddr addr);

const CsrDesc* find_csr_by_addr(CsrAddr addr);
const CsrDesc* find_csr_by_name(std::string_view name);

/// True if the CSR is architecturally read-only, which is encoded in bits
/// [11:10] of its address. `cycle` and `instret` fall in this range, so a
/// csrrw targeting them is an illegal instruction rather than a silent no-op.
constexpr bool csr_is_read_only(CsrAddr addr) { return ((addr >> 10) & 3u) == 3u; }

}  // namespace rv::isa
