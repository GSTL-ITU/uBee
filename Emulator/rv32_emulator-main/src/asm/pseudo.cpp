#include "asm/pseudo.hpp"

#include <cassert>
#include <unordered_map>
#include <vector>

namespace rv::as {
namespace {

/// Spelling -> every row with that spelling. Small vectors: no spelling has
/// more than two arities.
const std::unordered_map<std::string_view, std::vector<PseudoId>>& spelling_index() {
    static const std::unordered_map<std::string_view, std::vector<PseudoId>> index = [] {
        std::unordered_map<std::string_view, std::vector<PseudoId>> map;
        for (std::size_t i = 0; i < kPseudoCount; ++i) {
            map[kPseudoTable[i].spelling].push_back(static_cast<PseudoId>(i));
        }
        return map;
    }();
    return index;
}

}  // namespace

const PseudoDesc& describe_pseudo(PseudoId id) {
    const auto index = static_cast<std::size_t>(id);
    assert(index < kPseudoCount && "describe_pseudo() called with kInvalidPseudo");
    return kPseudoTable[index];
}

PseudoId lookup_pseudo(std::string_view spelling, std::size_t operand_count) {
    const auto& index = spelling_index();
    const auto it = index.find(spelling);
    if (it == index.end()) return kInvalidPseudo;
    for (const PseudoId id : it->second) {
        if (describe_pseudo(id).operand_count == operand_count) return id;
    }
    return kInvalidPseudo;
}

bool is_pseudo_spelling(std::string_view spelling) {
    return spelling_index().count(spelling) != 0;
}

}  // namespace rv::as
