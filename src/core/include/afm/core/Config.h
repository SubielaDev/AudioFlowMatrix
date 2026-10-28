#pragma once

#include <cstddef>
#include <cstdint>

namespace afm {

// Fixed instead of std::hardware_destructive_interference_size, which triggers ABI
// warnings on GCC and is 64 on every x86-64 target we care about anyway.
inline constexpr std::size_t kCacheLineSize = 64;

// One AVX2 register (8 x float).
inline constexpr std::size_t kSimdAlignment = 32;

// Upper bounds for the routing matrix and per-block scratch memory. Everything is
// sized from these at prepare() time so the render loop never allocates.
inline constexpr std::uint32_t kMaxInputs = 64;
inline constexpr std::uint32_t kMaxOutputs = 64;
inline constexpr std::uint32_t kMaxBlockFrames = 2048;

} // namespace afm
