#pragma once

#include "afm/core/AlignedBuffer.h"
#include "afm/core/AudioRingCore.h"

#include <cstddef>
#include <cstdint>

namespace afm {

// Heap-backed lock-free SPSC FIFO for multichannel float audio, used wherever two clock
// domains or two threads meet inside the process: WASAPI mic -> resampler -> engine,
// engine -> resampler -> WASAPI render (Discord/OBS). See AudioRingCore for semantics.
class AudioRingBuffer {
public:
    // Not real-time safe: allocates.
    AudioRingBuffer(std::uint32_t numChannels, std::size_t minCapacityFrames)
        : core_(numChannels, AudioRingCore::roundCapacity(minCapacityFrames)),
          storage_(AudioRingCore::storageFloats(numChannels, core_.capacityFrames())) {}

    AudioRingBuffer(const AudioRingBuffer&) = delete;
    AudioRingBuffer& operator=(const AudioRingBuffer&) = delete;

    [[nodiscard]] std::uint32_t numChannels() const noexcept { return core_.numChannels(); }
    [[nodiscard]] std::size_t capacityFrames() const noexcept { return core_.capacityFrames(); }
    [[nodiscard]] std::size_t fillLevelFrames() const noexcept { return core_.fillLevelFrames(); }

    // Producer
    [[nodiscard]] std::size_t writableFrames() noexcept { return core_.writableFrames(); }
    std::size_t write(ConstAudioBlock src) noexcept { return core_.write(storage_.data(), src); }
    std::size_t writeSilence(std::size_t frames) noexcept { return core_.writeSilence(storage_.data(), frames); }
    [[nodiscard]] std::uint64_t overruns() const noexcept { return core_.overruns(); }

    // Consumer
    [[nodiscard]] std::size_t readableFrames() noexcept { return core_.readableFrames(); }
    std::size_t read(AudioBlock dst) noexcept { return core_.read(storage_.data(), dst); }
    std::size_t discard(std::size_t frames) noexcept { return core_.discard(frames); }
    [[nodiscard]] std::uint64_t underruns() const noexcept { return core_.underruns(); }

    // Not thread-safe: only call while neither side is running.
    void reset() noexcept {
        core_.reset();
        storage_.clear();
    }

private:
    AudioRingCore core_;
    AlignedBuffer<float> storage_;
};

} // namespace afm
