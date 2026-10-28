#pragma once

#include "afm/core/Config.h"

#include <array>
#include <cstdint>

namespace afm::dsp {

// Linear gain of every crosspoint, gain[out][in]. 0 means "not routed". Built on the
// control thread from the user-facing RoutingModel and handed to the audio thread
// through a TripleBuffer.
struct MixCoefficients {
    std::uint32_t numInputs = 0;
    std::uint32_t numOutputs = 0;
    float gain[kMaxOutputs][kMaxInputs] = {};
};

// NxM routing matrix: out[o] = sum_i gain[o][i] * in[i].
//
// - Sparse: each output only visits the inputs routed to it.
// - AVX2 + FMA kernel processing 32 frames per iteration (4 accumulators), selected at
//   runtime; a scalar kernel covers CPUs without AVX2.
// - Click-free: when coefficients change, every affected crosspoint ramps linearly from
//   the old to the new gain over one block.
class MatrixMixer {
public:
    MatrixMixer() noexcept;

    // Real-time safe. Takes effect (with a ramp) on the next process() call.
    void setTarget(const MixCoefficients& coefficients) noexcept;

    // Real-time safe. `in` has numIn lanes, `out` numOut lanes of `frames` floats. Every
    // output lane is written. If the current coefficients were built for a different
    // bus layout, all outputs are silent.
    void process(const float* const* in, std::uint32_t numIn, float* const* out, std::uint32_t numOut,
                 std::uint32_t frames) noexcept;

    [[nodiscard]] static bool usingAvx2() noexcept;

private:
    struct Route {
        std::uint8_t input;
        float from;
        float to;
    };

    std::uint32_t numInputs_ = 0;
    std::uint32_t numOutputs_ = 0;
    float current_[kMaxOutputs][kMaxInputs] = {};
    float target_[kMaxOutputs][kMaxInputs] = {};
    bool pending_ = false;

    // Per-output list of active routes for this block, rebuilt only when pending_.
    std::array<std::array<Route, kMaxInputs>, kMaxOutputs> routes_{};
    std::array<std::uint32_t, kMaxOutputs> routeCount_{};
    std::array<bool, kMaxOutputs> ramping_{};
};

} // namespace afm::dsp
