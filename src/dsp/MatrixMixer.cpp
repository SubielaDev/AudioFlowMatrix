#include "afm/dsp/MatrixMixer.h"

#include <cstring>

#if defined(_M_X64) || defined(__x86_64__)
#define AFM_X64 1
#include <immintrin.h>
#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
#define AFM_TARGET_AVX2
#else
#include <cpuid.h>
#define AFM_TARGET_AVX2 __attribute__((target("avx2,fma")))
#endif
#endif

namespace afm::dsp {

namespace {

struct RouteView {
    const float* in;
    float from;
    float delta;  // (to - from) / frames
};

bool detectAvx2() noexcept {
#if !defined(AFM_X64)
    return false;
#elif defined(_MSC_VER) && !defined(__clang__)
    int r[4];
    __cpuid(r, 0);
    if (r[0] < 7) return false;
    __cpuid(r, 1);
    const bool fma = (r[2] & (1 << 12)) != 0;
    const bool osxsave = (r[2] & (1 << 27)) != 0;
    const bool avx = (r[2] & (1 << 28)) != 0;
    if (!fma || !osxsave || !avx || (_xgetbv(0) & 6) != 6) return false;
    __cpuidex(r, 7, 0);
    return (r[1] & (1 << 5)) != 0;
#else
    // Raw CPUID/XGETBV: __builtin_cpu_supports needs a compiler-rt symbol not every
    // toolchain ships.
    unsigned a = 0, b = 0, c = 0, d = 0;
    if (__get_cpuid_max(0, nullptr) < 7 || !__get_cpuid(1, &a, &b, &c, &d)) return false;
    const bool fma = (c & (1u << 12)) != 0;
    const bool osxsave = (c & (1u << 27)) != 0;
    const bool avx = (c & (1u << 28)) != 0;
    if (!fma || !osxsave || !avx) return false;
    unsigned xcr0 = 0, xcr0High = 0;
    __asm__ volatile("xgetbv" : "=a"(xcr0), "=d"(xcr0High) : "c"(0));
    if ((xcr0 & 6u) != 6u) return false;  // OS saves XMM and YMM state
    __cpuid_count(7, 0, a, b, c, d);
    return (b & (1u << 5)) != 0;
#endif
}

const bool kHasAvx2 = detectAvx2();

void mixScalar(const RouteView* routes, std::uint32_t count, float* out, std::uint32_t frames, bool ramp) noexcept {
    std::memset(out, 0, frames * sizeof(float));
    for (std::uint32_t j = 0; j < count; ++j) {
        const RouteView& r = routes[j];
        if (ramp) {
            for (std::uint32_t f = 0; f < frames; ++f) {
                out[f] += (r.from + r.delta * static_cast<float>(f + 1)) * r.in[f];
            }
        } else {
            for (std::uint32_t f = 0; f < frames; ++f) out[f] += r.from * r.in[f];
        }
    }
}

#if defined(AFM_X64)
AFM_TARGET_AVX2 void mixAvx2(const RouteView* routes, std::uint32_t count, float* out, std::uint32_t frames,
                             bool ramp) noexcept {
    std::uint32_t f = 0;
    if (!ramp) {
        // 32 frames per iteration, 4 independent accumulators to hide FMA latency.
        for (; f + 32 <= frames; f += 32) {
            __m256 a0 = _mm256_setzero_ps(), a1 = _mm256_setzero_ps();
            __m256 a2 = _mm256_setzero_ps(), a3 = _mm256_setzero_ps();
            for (std::uint32_t j = 0; j < count; ++j) {
                const __m256 g = _mm256_set1_ps(routes[j].from);
                const float* p = routes[j].in + f;
                a0 = _mm256_fmadd_ps(g, _mm256_loadu_ps(p), a0);
                a1 = _mm256_fmadd_ps(g, _mm256_loadu_ps(p + 8), a1);
                a2 = _mm256_fmadd_ps(g, _mm256_loadu_ps(p + 16), a2);
                a3 = _mm256_fmadd_ps(g, _mm256_loadu_ps(p + 24), a3);
            }
            _mm256_storeu_ps(out + f, a0);
            _mm256_storeu_ps(out + f + 8, a1);
            _mm256_storeu_ps(out + f + 16, a2);
            _mm256_storeu_ps(out + f + 24, a3);
        }
        for (; f + 8 <= frames; f += 8) {
            __m256 a = _mm256_setzero_ps();
            for (std::uint32_t j = 0; j < count; ++j) {
                a = _mm256_fmadd_ps(_mm256_set1_ps(routes[j].from), _mm256_loadu_ps(routes[j].in + f), a);
            }
            _mm256_storeu_ps(out + f, a);
        }
        for (; f < frames; ++f) {
            float s = 0.0f;
            for (std::uint32_t j = 0; j < count; ++j) s += routes[j].from * routes[j].in[f];
            out[f] = s;
        }
        return;
    }

    // Ramp: gain(k) = from + delta * (k + 1), reaching `to` on the last frame.
    const __m256 step = _mm256_setr_ps(1, 2, 3, 4, 5, 6, 7, 8);
    for (; f + 8 <= frames; f += 8) {
        const __m256 k = _mm256_add_ps(_mm256_set1_ps(static_cast<float>(f)), step);
        __m256 a = _mm256_setzero_ps();
        for (std::uint32_t j = 0; j < count; ++j) {
            const __m256 g = _mm256_fmadd_ps(_mm256_set1_ps(routes[j].delta), k, _mm256_set1_ps(routes[j].from));
            a = _mm256_fmadd_ps(g, _mm256_loadu_ps(routes[j].in + f), a);
        }
        _mm256_storeu_ps(out + f, a);
    }
    for (; f < frames; ++f) {
        float s = 0.0f;
        for (std::uint32_t j = 0; j < count; ++j) {
            s += (routes[j].from + routes[j].delta * static_cast<float>(f + 1)) * routes[j].in[f];
        }
        out[f] = s;
    }
}
#endif

} // namespace

MatrixMixer::MatrixMixer() noexcept = default;

bool MatrixMixer::usingAvx2() noexcept { return kHasAvx2; }

void MatrixMixer::setTarget(const MixCoefficients& c) noexcept {
    numInputs_ = c.numInputs <= kMaxInputs ? c.numInputs : kMaxInputs;
    numOutputs_ = c.numOutputs <= kMaxOutputs ? c.numOutputs : kMaxOutputs;
    std::memcpy(target_, c.gain, sizeof(target_));
    pending_ = true;
}

void MatrixMixer::process(const float* const* in, std::uint32_t numIn, float* const* out, std::uint32_t numOut,
                          std::uint32_t frames) noexcept {
    if (frames == 0) return;

    bool rampedThisBlock = false;
    if (pending_) {
        for (std::uint32_t o = 0; o < numOutputs_; ++o) {
            std::uint32_t count = 0;
            bool ramp = false;
            for (std::uint32_t i = 0; i < numInputs_; ++i) {
                const float from = current_[o][i];
                const float to = target_[o][i];
                if (from != 0.0f || to != 0.0f) {
                    routes_[o][count++] = {static_cast<std::uint8_t>(i), from, to};
                    ramp = ramp || from != to;
                }
            }
            routeCount_[o] = count;
            ramping_[o] = ramp;
            rampedThisBlock = rampedThisBlock || ramp;
        }
        std::memcpy(current_, target_, sizeof(current_));
        pending_ = false;
    }

    // A snapshot built for a different bus layout is never applied to this one.
    const bool layoutMatches = numIn == numInputs_ && numOut == numOutputs_;
    const float invFrames = 1.0f / static_cast<float>(frames);
    RouteView views[kMaxInputs];

    for (std::uint32_t o = 0; o < numOut; ++o) {
        const std::uint32_t count = layoutMatches ? routeCount_[o] : 0;
        if (count == 0) {
            std::memset(out[o], 0, frames * sizeof(float));
            continue;
        }
        const bool ramp = ramping_[o];
        for (std::uint32_t j = 0; j < count; ++j) {
            const Route& r = routes_[o][j];
            views[j] = {in[r.input], ramp ? r.from : r.to, ramp ? (r.to - r.from) * invFrames : 0.0f};
        }
#if defined(AFM_X64)
        if (kHasAvx2) {
            mixAvx2(views, count, out[o], frames, ramp);
            continue;
        }
#endif
        mixScalar(views, count, out[o], frames, ramp);
    }

    // Ramps last one block; next block runs the steady-state (non-ramp) kernel.
    if (rampedThisBlock) {
        pending_ = true;
    }
}

} // namespace afm::dsp
