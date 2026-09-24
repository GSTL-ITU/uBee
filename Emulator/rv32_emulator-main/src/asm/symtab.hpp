// Symbols, and the address space each one lives in.
//
// The Space tag is the load-bearing part. IMEM and DMEM are separate address
// spaces that both start at zero, so `msg` in .data and `loop` in .text can
// have the *same numeric value* and mean completely different locations.
// Without the tag, `call msg` would assemble cleanly and jump somewhere
// insane. With it, that is a compile error that teaches the architecture.
#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "asm/source.hpp"
#include "isa/types.hpp"

namespace rv::as {

enum class Space : u8 {
    Text,      // an address in instruction memory
    Data,      // an address in data memory
    Absolute,  // a plain constant from .equ -- belongs to no space
};

const char* space_name(Space space);

struct Symbol {
    std::string name;
    Space space = Space::Text;
    Addr value = 0;
    bool is_global = false;
    SourceSpan definition;
};

class SymbolTable {
public:
    const Symbol* find(std::string_view name) const;
    Symbol* find(std::string_view name);

    /// Define a symbol. Returns nullptr if the name already exists, leaving
    /// the original in place so the caller can point at both definitions.
    Symbol* define(std::string name, Space space, Addr value, SourceSpan where);

    void mark_global(std::string_view name);

    const std::vector<Symbol>& all() const { return symbols_; }

    /// Every defined name, for the "did you mean" suggester.
    std::vector<std::string_view> names() const;

    void clear();

private:
    std::vector<Symbol> symbols_;
    // Keys are owned by the map, not borrowed from the vector: the vector
    // reallocates as symbols are added, and a moved std::string with a small
    // buffer would leave any borrowed view dangling.
    std::unordered_map<std::string, std::size_t> index_;
};

}  // namespace rv::as
