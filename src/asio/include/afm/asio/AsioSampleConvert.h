#pragma once

#include "afm/asio/AsioAbi.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace afm::asio {

// Conversions between ASIO little-endian sample formats and the engine's float format.
// All functions are noexcept and allocation-free; they run inside bufferSwitch.

[[nodiscard]] inline bool isSupportedSampleType(ASIOSampleType type) noexcept {
    switch (type) {
    case ASIOSTInt16LSB:
    case ASIOSTInt24LSB:
    case ASIOSTInt32LSB:
    case ASIOSTFloat32LSB:
    case ASIOSTFloat64LSB:
    case ASIOSTInt32LSB16:
    case ASIOSTInt32LSB18:
    case ASIOSTInt32LSB20:
    case ASIOSTInt32LSB24:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] inline const char* sampleTypeName(ASIOSampleType type) noexcept {
    switch (type) {
    case ASIOSTInt16LSB: return "Int16";
    case ASIOSTInt24LSB: return "Int24";
    case ASIOSTInt32LSB: return "Int32";
    case ASIOSTFloat32LSB: return "Float32";
    case ASIOSTFloat64LSB: return "Float64";
    case ASIOSTInt32LSB16: return "Int32/16";
    case ASIOSTInt32LSB18: return "Int32/18";
    case ASIOSTInt32LSB20: return "Int32/20";
    case ASIOSTInt32LSB24: return "Int32/24";
    default: return "unsupported";
    }
}

namespace detail {

template <int Bits>
inline void int32ToFloat(const std::int32_t* src, float* dst, long n) noexcept {
    constexpr float scale = 1.0f / static_cast<float>(1ll << (Bits - 1));
    for (long i = 0; i < n; ++i) {
        dst[i] = static_cast<float>(src[i]) * scale;
    }
}

template <int Bits>
inline void floatToInt32(const float* src, std::int32_t* dst, long n) noexcept {
    constexpr double scale = static_cast<double>((1ll << (Bits - 1)) - 1);
    for (long i = 0; i < n; ++i) {
        const double v = std::clamp(static_cast<double>(src[i]), -1.0, 1.0);
        dst[i] = static_cast<std::int32_t>(v * scale);
    }
}

} // namespace detail

inline void toFloat(ASIOSampleType type, const void* src, float* dst, long n) noexcept {
    switch (type) {
    case ASIOSTInt16LSB: {
        const auto* s = static_cast<const std::int16_t*>(src);
        for (long i = 0; i < n; ++i) dst[i] = static_cast<float>(s[i]) * (1.0f / 32768.0f);
        break;
    }
    case ASIOSTInt24LSB: {
        const auto* s = static_cast<const std::uint8_t*>(src);
        for (long i = 0; i < n; ++i, s += 3) {
            const std::int32_t v = static_cast<std::int32_t>(
                (static_cast<std::uint32_t>(s[0]) << 8) | (static_cast<std::uint32_t>(s[1]) << 16) |
                (static_cast<std::uint32_t>(s[2]) << 24));
            dst[i] = static_cast<float>(v >> 8) * (1.0f / 8388608.0f);
        }
        break;
    }
    case ASIOSTInt32LSB: detail::int32ToFloat<32>(static_cast<const std::int32_t*>(src), dst, n); break;
    case ASIOSTInt32LSB16: detail::int32ToFloat<16>(static_cast<const std::int32_t*>(src), dst, n); break;
    case ASIOSTInt32LSB18: detail::int32ToFloat<18>(static_cast<const std::int32_t*>(src), dst, n); break;
    case ASIOSTInt32LSB20: detail::int32ToFloat<20>(static_cast<const std::int32_t*>(src), dst, n); break;
    case ASIOSTInt32LSB24: detail::int32ToFloat<24>(static_cast<const std::int32_t*>(src), dst, n); break;
    case ASIOSTFloat32LSB: std::memcpy(dst, src, static_cast<std::size_t>(n) * sizeof(float)); break;
    case ASIOSTFloat64LSB: {
        const auto* s = static_cast<const double*>(src);
        for (long i = 0; i < n; ++i) dst[i] = static_cast<float>(s[i]);
        break;
    }
    default: std::memset(dst, 0, static_cast<std::size_t>(n) * sizeof(float)); break;
    }
}

inline void fromFloat(ASIOSampleType type, const float* src, void* dst, long n) noexcept {
    switch (type) {
    case ASIOSTInt16LSB: {
        auto* d = static_cast<std::int16_t*>(dst);
        for (long i = 0; i < n; ++i) {
            d[i] = static_cast<std::int16_t>(std::clamp(src[i], -1.0f, 1.0f) * 32767.0f);
        }
        break;
    }
    case ASIOSTInt24LSB: {
        auto* d = static_cast<std::uint8_t*>(dst);
        for (long i = 0; i < n; ++i, d += 3) {
            const auto v = static_cast<std::int32_t>(std::clamp(src[i], -1.0f, 1.0f) * 8388607.0f);
            d[0] = static_cast<std::uint8_t>(v);
            d[1] = static_cast<std::uint8_t>(v >> 8);
            d[2] = static_cast<std::uint8_t>(v >> 16);
        }
        break;
    }
    case ASIOSTInt32LSB: detail::floatToInt32<32>(src, static_cast<std::int32_t*>(dst), n); break;
    case ASIOSTInt32LSB16: detail::floatToInt32<16>(src, static_cast<std::int32_t*>(dst), n); break;
    case ASIOSTInt32LSB18: detail::floatToInt32<18>(src, static_cast<std::int32_t*>(dst), n); break;
    case ASIOSTInt32LSB20: detail::floatToInt32<20>(src, static_cast<std::int32_t*>(dst), n); break;
    case ASIOSTInt32LSB24: detail::floatToInt32<24>(src, static_cast<std::int32_t*>(dst), n); break;
    case ASIOSTFloat32LSB: std::memcpy(dst, src, static_cast<std::size_t>(n) * sizeof(float)); break;
    case ASIOSTFloat64LSB: {
        auto* d = static_cast<double*>(dst);
        for (long i = 0; i < n; ++i) d[i] = static_cast<double>(src[i]);
        break;
    }
    default: break;
    }
}

[[nodiscard]] inline long bytesPerSample(ASIOSampleType type) noexcept {
    switch (type) {
    case ASIOSTInt16LSB: return 2;
    case ASIOSTInt24LSB: return 3;
    case ASIOSTFloat64LSB: return 8;
    default: return 4;
    }
}

} // namespace afm::asio
