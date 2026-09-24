#pragma once

#include <array>
#include <cstddef>
#include <string_view>

#include "isa/types.hpp"

namespace rv::as {

enum class DirectiveId : u8 {
#define RV_DIR(id, spelling) id,
#include "asm/directive_table.def"
#undef RV_DIR
    Count
};

inline constexpr std::size_t kDirectiveCount = static_cast<std::size_t>(DirectiveId::Count);
inline constexpr DirectiveId kInvalidDirective = DirectiveId::Count;

inline constexpr std::array<std::string_view, kDirectiveCount> kDirectiveNames = {{
#define RV_DIR(id, spelling) spelling,
#include "asm/directive_table.def"
#undef RV_DIR
}};

/// Returns kInvalidDirective for a word that is not a directive -- which is how
/// the lexer tells `.text` (a directive) from `.L1` (a local label).
DirectiveId lookup_directive(std::string_view spelling);

constexpr std::string_view directive_name(DirectiveId id) {
    return id == kInvalidDirective ? "?" : kDirectiveNames[static_cast<std::size_t>(id)];
}

}  // namespace rv::as
