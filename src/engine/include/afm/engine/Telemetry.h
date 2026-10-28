#pragma once

#include "afm/core/Config.h"

#include <array>
#include <cstdint>

namespace afm {

// One snapshot per processed block, pushed from the audio thread to the GUI through an
// SpscRingBuffer. Plain data only, so it can be memcpy'd.
struct TelemetryFrame {
    std::uint64_t blockIndex = 0;
    float dspLoad = 0.0f;  // time spent in process() / block period
    std::uint32_t numInputs = 0;
    std::uint32_t numOutputs = 0;
    std::array<float, kMaxInputs> inputPeak{};
    std::array<float, kMaxOutputs> outputPeak{};
};

} // namespace afm
