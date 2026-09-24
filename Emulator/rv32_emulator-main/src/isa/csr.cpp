#include "isa/csr.hpp"

#include <unordered_map>

namespace rv::isa {
namespace {

/// Address -> table index. 4096 entries of two bytes is 8 KB for an O(1)
/// lookup with no hashing on the CSR execution path.
using AddrIndex = std::array<u16, 4096>;

const AddrIndex& addr_index() {
    static const AddrIndex index = [] {
        AddrIndex map{};
        map.fill(static_cast<u16>(kCsrCount));
        for (std::size_t i = 0; i < kCsrCount; ++i) map[kCsrTable[i].addr] = static_cast<u16>(i);
        return map;
    }();
    return index;
}

}  // namespace

std::size_t csr_index(CsrAddr addr) {
    if (addr >= 4096) return kCsrCount;
    return addr_index()[addr];
}

const CsrDesc* find_csr_by_addr(CsrAddr addr) {
    const std::size_t index = csr_index(addr);
    return index == kCsrCount ? nullptr : &kCsrTable[index];
}

const CsrDesc* find_csr_by_name(std::string_view name) {
    static const std::unordered_map<std::string_view, const CsrDesc*> index = [] {
        std::unordered_map<std::string_view, const CsrDesc*> map;
        for (const CsrDesc& desc : kCsrTable) map.emplace(desc.name, &desc);
        return map;
    }();
    const auto it = index.find(name);
    return it == index.end() ? nullptr : it->second;
}

}  // namespace rv::isa
