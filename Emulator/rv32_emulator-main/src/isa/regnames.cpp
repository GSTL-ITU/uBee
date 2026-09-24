#include "regnames.hpp"

#include <unordered_map>

namespace rv::isa {
namespace {

const std::unordered_map<std::string_view, RegIdx>& register_index() {
    static const std::unordered_map<std::string_view, RegIdx> index = [] {
        std::unordered_map<std::string_view, RegIdx> map;
        map.reserve(kNumRegs * 3);
        for (RegIdx r = 0; r < kNumRegs; ++r) {
            map.emplace(kNumericNames[r], r);
            map.emplace(kAbiNames[r], r);
        }
        // `fp` is the conventional alias for s0 when a frame pointer is in use.
        map.emplace("fp", RegIdx{8});
        return map;
    }();
    return index;
}

}  // namespace

std::optional<RegIdx> parse_register(std::string_view name) {
    const auto& index = register_index();
    const auto it = index.find(name);
    if (it == index.end()) return std::nullopt;
    return it->second;
}

}  // namespace rv::isa
