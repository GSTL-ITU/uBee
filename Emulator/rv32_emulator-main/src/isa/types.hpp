// Core integer types and bit manipulation primitives for the RV32 emulator.
//
// Everything here is constexpr and header-only: these helpers sit in the
// innermost loop of the decoder and encoder, and having them constant-foldable
// keeps the generated instruction tables free of runtime initialisation.
#pragma once

#include <cstdint>

namespace rv {

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using i8 = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;

/// A byte address inside one of the two memory spaces (IMEM or DMEM).
using Addr = u32;
/// A 32-bit machine word: an encoded instruction, or a datum.
using Word = u32;
/// An architectural register number, 0..31.
using RegIdx = u8;
/// A CSR address, 0..4095.
using CsrAddr = u16;

inline constexpr int kXlen = 32;
inline constexpr int kNumRegs = 32;

/// Extract bits [hi:lo] of `value`, shifted down to bit 0.
constexpr u32 bits(u32 value, int hi, int lo) {
    // Width can be 32, and `1u << 32` is undefined, so build the mask by
    // shifting a 64-bit one and truncating.
    const u64 width = static_cast<u64>(hi - lo + 1);
    const u64 mask = (u64{1} << width) - 1;
    return static_cast<u32>((value >> lo) & mask);
}

/// Extract a single bit as 0 or 1.
constexpr u32 bit(u32 value, int index) { return (value >> index) & 1u; }

/// Sign-extend the low `width` bits of `value` to a full 32-bit signed integer.
constexpr i32 sign_extend(u32 value, int width) {
    if (width >= kXlen) return static_cast<i32>(value);
    const u32 sign_bit = u32{1} << (width - 1);
    const u32 mask = (u32{1} << width) - 1;
    const u32 truncated = value & mask;
    // Flip-and-subtract: branch-free and well-defined for all widths, unlike
    // the `<< n >> n` trick which relies on implementation-defined right shift.
    return static_cast<i32>((truncated ^ sign_bit) - sign_bit);
}

// RISC-V shifts use only the low 5 bits of the shift amount. Applying a raw
// amount to a C++ shift operator would be undefined behaviour for amounts >= 32,
// so every shift in the executor goes through these.

constexpr u32 shift_left(u32 value, u32 amount) { return value << (amount & 31u); }

constexpr u32 shift_right_logical(u32 value, u32 amount) { return value >> (amount & 31u); }

constexpr u32 shift_right_arith(u32 value, u32 amount) {
    // Implementation-defined behaviour for `i32 >> n` is arithmetic shift on
    // every compiler we target, but doing it on the unsigned value and pasting
    // the sign bits back is portable and just as cheap.
    const u32 sh = amount & 31u;
    const u32 logical = value >> sh;
    if ((value & 0x8000'0000u) == 0 || sh == 0) return logical;
    return logical | (~0u << (32 - sh));
}

/// True if `value` fits in a `width`-bit signed field.
constexpr bool fits_signed(i64 value, int width) {
    const i64 limit = i64{1} << (width - 1);
    return value >= -limit && value < limit;
}

/// True if `value` fits in a `width`-bit unsigned field.
constexpr bool fits_unsigned(u64 value, int width) {
    return value < (u64{1} << width);
}

}  // namespace rv
