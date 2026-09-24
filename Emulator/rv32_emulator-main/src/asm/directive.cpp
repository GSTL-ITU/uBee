#include "asm/directive.hpp"

#include <unordered_map>

namespace rv::as {

DirectiveId lookup_directive(std::string_view spelling) {
    static const std::unordered_map<std::string_view, DirectiveId> index = [] {
        std::unordered_map<std::string_view, DirectiveId> map;
        for (std::size_t i = 0; i < kDirectiveCount; ++i) {
            map.emplace(kDirectiveNames[i], static_cast<DirectiveId>(i));
        }
        return map;
    }();
    const auto it = index.find(spelling);
    return it == index.end() ? kInvalidDirective : it->second;
}

}  // namespace rv::as
