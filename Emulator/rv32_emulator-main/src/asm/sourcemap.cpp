#include "asm/sourcemap.hpp"

#include <algorithm>

namespace rv::as {
namespace {

const std::vector<Addr>& empty_addrs() {
    static const std::vector<Addr> empty;
    return empty;
}

}  // namespace

void SourceMap::add(AddrEntry entry) { by_addr_.push_back(entry); }

void SourceMap::finalize() {
    std::sort(by_addr_.begin(), by_addr_.end(),
              [](const AddrEntry& a, const AddrEntry& b) { return a.addr < b.addr; });

    u32 highest_line = 0;
    for (const AddrEntry& entry : by_addr_) highest_line = std::max(highest_line, entry.line);

    line_to_addrs_.assign(highest_line, {});
    for (const AddrEntry& entry : by_addr_) {
        if (entry.line == 0) continue;
        line_to_addrs_[entry.line - 1].push_back(entry.addr);
    }
    for (std::vector<Addr>& addrs : line_to_addrs_) std::sort(addrs.begin(), addrs.end());
}

void SourceMap::clear() {
    by_addr_.clear();
    line_to_addrs_.clear();
}

std::optional<AddrEntry> SourceMap::at(Addr addr) const {
    const auto it = std::lower_bound(
        by_addr_.begin(), by_addr_.end(), addr,
        [](const AddrEntry& entry, Addr target) { return entry.addr < target; });
    if (it == by_addr_.end() || it->addr != addr) return std::nullopt;
    return *it;
}

std::optional<u32> SourceMap::line_of(Addr addr) const {
    if (const auto entry = at(addr)) return entry->line;
    return std::nullopt;
}

const std::vector<Addr>& SourceMap::addrs_for_line(u32 line) const {
    if (line == 0 || line > line_to_addrs_.size()) return empty_addrs();
    return line_to_addrs_[line - 1];
}

bool SourceMap::is_line_start(Addr addr) const {
    const auto entry = at(addr);
    return entry.has_value() && entry->slot == 0;
}

std::optional<u32> SourceMap::next_line_with_code(u32 line) const {
    for (u32 candidate = line; candidate <= line_to_addrs_.size(); ++candidate) {
        if (!line_to_addrs_[candidate - 1].empty()) return candidate;
    }
    return std::nullopt;
}

std::optional<Addr> SourceMap::first_addr_at_or_after(u32 line) const {
    if (const auto found = next_line_with_code(line)) {
        return line_to_addrs_[*found - 1].front();
    }
    return std::nullopt;
}

}  // namespace rv::as
