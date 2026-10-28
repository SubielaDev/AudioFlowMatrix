#pragma once

#include "afm/core/AudioRingBuffer.h"
#include "afm/engine/IAudioEndpoint.h"

#include <string>
#include <utility>

namespace afm {

// Input endpoint backed by an AudioRingBuffer. Another thread (virtual ASIO client
// callback, WASAPI capture + resampler) is the producer via ring(); the engine is the
// consumer via pull().
class RingInputEndpoint final : public IInputEndpoint {
public:
    RingInputEndpoint(std::string name, std::uint32_t numChannels, std::size_t capacityFrames)
        : name_(std::move(name)), ring_(numChannels, capacityFrames) {}

    [[nodiscard]] std::string_view name() const noexcept override { return name_; }
    [[nodiscard]] std::uint32_t numChannels() const noexcept override { return ring_.numChannels(); }
    void pull(AudioBlock dst) noexcept override { ring_.read(dst); }

    [[nodiscard]] AudioRingBuffer& ring() noexcept { return ring_; }

private:
    std::string name_;
    AudioRingBuffer ring_;
};

// Output endpoint backed by an AudioRingBuffer. The engine is the producer via push();
// a device thread (WASAPI render, virtual ASIO client callback) consumes via ring().
class RingOutputEndpoint final : public IOutputEndpoint {
public:
    RingOutputEndpoint(std::string name, std::uint32_t numChannels, std::size_t capacityFrames)
        : name_(std::move(name)), ring_(numChannels, capacityFrames) {}

    [[nodiscard]] std::string_view name() const noexcept override { return name_; }
    [[nodiscard]] std::uint32_t numChannels() const noexcept override { return ring_.numChannels(); }
    void push(ConstAudioBlock src) noexcept override { ring_.write(src); }

    [[nodiscard]] AudioRingBuffer& ring() noexcept { return ring_; }

private:
    std::string name_;
    AudioRingBuffer ring_;
};

} // namespace afm
