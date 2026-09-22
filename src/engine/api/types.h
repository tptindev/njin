#pragma once

#include <cstddef>
#include <cstdint>

namespace njin {
// Signed integers
using i8 = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;

// Unsigned integers
using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

// Floating point
using f32 = float;
using f64 = double;

// Pointer-sized integers
using usize = std::size_t;
using isize = std::ptrdiff_t;
} // namespace njin
