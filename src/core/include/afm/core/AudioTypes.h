#pragma once

#include <cstdint>

namespace afm {

struct StreamFormat {
    double sampleRate = 48000.0;
    std::uint32_t blockFrames = 128;
};

// Non-owning view over planar (one pointer per channel) float audio.
struct AudioBlock {
    float* const* channels = nullptr;
    std::uint32_t numChannels = 0;
    std::uint32_t numFrames = 0;
};

struct ConstAudioBlock {
    const float* const* channels = nullptr;
    std::uint32_t numChannels = 0;
    std::uint32_t numFrames = 0;

    ConstAudioBlock() noexcept = default;
    ConstAudioBlock(const float* const* ch, std::uint32_t numCh, std::uint32_t frames) noexcept
        : channels(ch), numChannels(numCh), numFrames(frames) {}
    ConstAudioBlock(AudioBlock block) noexcept // NOLINT(google-explicit-constructor)
        : channels(block.channels), numChannels(block.numChannels), numFrames(block.numFrames) {}
};

} // namespace afm
