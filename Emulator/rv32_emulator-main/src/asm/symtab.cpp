#include "asm/symtab.hpp"

namespace rv::as {

const char* space_name(Space space) {
    switch (space) {
        case Space::Text: return "text";
        case Space::Data: return "data";
        case Space::Absolute: return "absolute";
    }
    return "?";
}

const Symbol* SymbolTable::find(std::string_view name) const {
    const auto it = index_.find(std::string(name));
    return it == index_.end() ? nullptr : &symbols_[it->second];
}

Symbol* SymbolTable::find(std::string_view name) {
    const auto it = index_.find(std::string(name));
    return it == index_.end() ? nullptr : &symbols_[it->second];
}

Symbol* SymbolTable::define(std::string name, Space space, Addr value, SourceSpan where) {
    if (index_.count(name) != 0) return nullptr;
    const std::size_t position = symbols_.size();
    symbols_.push_back(Symbol{name, space, value, false, where});
    index_.emplace(std::move(name), position);
    return &symbols_[position];
}

void SymbolTable::mark_global(std::string_view name) {
    if (Symbol* symbol = find(name)) symbol->is_global = true;
}

std::vector<std::string_view> SymbolTable::names() const {
    std::vector<std::string_view> out;
    out.reserve(symbols_.size());
    for (const Symbol& symbol : symbols_) out.push_back(symbol.name);
    return out;
}

void SymbolTable::clear() {
    symbols_.clear();
    index_.clear();
}

}  // namespace rv::as
