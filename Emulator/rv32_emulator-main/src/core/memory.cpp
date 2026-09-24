#include "core/memory.hpp"

#include <algorithm>
#include <cassert>

namespace rv::core {
namespace {

/// RISC-V requires naturally aligned accesses unless the implementation chooses
/// to support misaligned ones. We trap, deliberately: on the FPGA core this
/// emulator is a reference for, a misaligned access is a real bug, and having
/// it surface here as a named trap teaches alignment instead of hiding it.
bool is_aligned(Addr addr, u8 width) { return (addr & (width - 1u)) == 0; }

}  // namespace

Memory::Memory(Addr base, u32 size) : bytes_(size, 0), base_(base), size_(size) {
    assert(size % 4 == 0 && "memory size must be a whole number of words");
}

MemResult Memory::read(Addr addr, u8 width, bool is_signed) const {
    if (!is_aligned(addr, width)) return MemResult::failure(TrapCause::LoadAddressMisaligned);
    if (!contains(addr) || !contains(addr + width - 1u)) {
        return MemResult::failure(TrapCause::LoadAccessFault);
    }

    const u32 offset = addr - base_;
    u32 value = 0;
    for (u8 i = 0; i < width; ++i) {
        value |= static_cast<u32>(bytes_[offset + i]) << (8 * i);  // little-endian
    }
    if (is_signed && width < 4) value = static_cast<u32>(sign_extend(value, 8 * width));
    return MemResult::success(value);
}

MemResult Memory::write(Addr addr, u8 width, u32 value) {
    if (!is_aligned(addr, width)) return MemResult::failure(TrapCause::StoreAddressMisaligned);
    if (!contains(addr) || !contains(addr + width - 1u)) {
        return MemResult::failure(TrapCause::StoreAccessFault);
    }

    const u32 offset = addr - base_;
    for (u8 i = 0; i < width; ++i) {
        bytes_[offset + i] = static_cast<u8>(value >> (8 * i));
    }
    high_water_ = std::max(high_water_, offset + width);
    return MemResult::success(0);
}

u32 Memory::read_word_raw(Addr addr) const {
    if (!contains(addr) || !contains(addr + 3u)) return 0;
    const u32 offset = addr - base_;
    return static_cast<u32>(bytes_[offset]) | (static_cast<u32>(bytes_[offset + 1]) << 8) |
           (static_cast<u32>(bytes_[offset + 2]) << 16) |
           (static_cast<u32>(bytes_[offset + 3]) << 24);
}

u16 Memory::read_half_raw(Addr addr) const {
    if (!contains(addr) || !contains(addr + 1u)) return 0;
    const u32 offset = addr - base_;
    return static_cast<u16>(static_cast<u32>(bytes_[offset]) |
                            (static_cast<u32>(bytes_[offset + 1]) << 8));
}

void Memory::write_word_raw(Addr addr, u32 value) {
    if (!contains(addr) || !contains(addr + 3u)) return;
    const u32 offset = addr - base_;
    for (u8 i = 0; i < 4; ++i) bytes_[offset + i] = static_cast<u8>(value >> (8 * i));
    high_water_ = std::max(high_water_, offset + 4u);
}

u8 Memory::read_byte_raw(Addr addr) const {
    if (!contains(addr)) return 0;
    return bytes_[addr - base_];
}

void Memory::load_words(const std::vector<u32>& words) {
    clear();
    const std::size_t count = std::min<std::size_t>(words.size(), size_ / 4);
    for (std::size_t i = 0; i < count; ++i) write_word_raw(base_ + static_cast<u32>(i * 4), words[i]);
}

void Memory::load_bytes(const std::vector<u8>& source) {
    clear();
    const std::size_t count = std::min<std::size_t>(source.size(), size_);
    std::copy_n(source.begin(), count, bytes_.begin());
    high_water_ = static_cast<u32>(count);
}

void Memory::clear() {
    std::fill(bytes_.begin(), bytes_.end(), u8{0});
    high_water_ = 0;
}

void Memory::resize(u32 size) {
    bytes_.resize(size, 0);
    size_ = size;
    if (high_water_ > size) high_water_ = size;
}

}  // namespace rv::core