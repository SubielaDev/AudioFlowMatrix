#pragma once

#include "afm/core/AudioTypes.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace afm {

// Anything that feeds audio into the matrix besides the master ASIO driver's own inputs:
// the virtual ASIO driver (Traktor's outputs), the WASAPI USB mic after resampling, ...
//
// pull() runs on the master ASIO callback thread. It must be noexcept, allocation-free,
// lock-free, and always fill all dst.numFrames frames (silence on underrun).
class IInputEndpoint {
public:
    virtual ~IInputEndpoint() = default;
    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
    [[nodiscard]] virtual std::uint32_t numChannels() const noexcept = 0;
    virtual void pull(AudioBlock dst) noexcept = 0;

    // Control/GUI thread only.
    [[nodiscard]] virtual std::string channelName(std::uint32_t ch) const {
        return std::string(name()) + " " + std::to_string(ch + 1);
    }
};

// Anything the matrix feeds besides the master driver's own outputs: the virtual ASIO
// driver (Traktor's inputs, e.g. DVS timecode), WASAPI render for Discord/OBS, ...
//
// push() runs on the master ASIO callback thread with the same constraints as pull().
class IOutputEndpoint {
public:
    virtual ~IOutputEndpoint() = default;
    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
    [[nodiscard]] virtual std::uint32_t numChannels() const noexcept = 0;
    virtual void push(ConstAudioBlock src) noexcept = 0;

    // Control/GUI thread only.
    [[nodiscard]] virtual std::string channelName(std::uint32_t ch) const {
        return std::string(name()) + " " + std::to_string(ch + 1);
    }
};

} // namespace afm
