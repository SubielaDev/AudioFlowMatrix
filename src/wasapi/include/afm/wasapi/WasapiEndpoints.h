#pragma once

#include "afm/core/AudioRingBuffer.h"
#include "afm/dsp/DriftResampler.h"
#include "afm/engine/IAudioEndpoint.h"
#include "afm/platform/Win.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace afm::wasapi {

struct DeviceInfo {
    std::wstring id;
    std::string name;
};

// Active WASAPI endpoints (capture or render), with friendly names.
[[nodiscard]] std::vector<DeviceInfo> listDevices(bool capture);

struct StreamConfig {
    std::wstring deviceId;
    std::string label;          // channel name prefix in the matrix, e.g. "Mic"
    bool exclusive = true;      // falls back to shared mode if exclusive is refused
    double engineRate = 48000.0;
    std::uint32_t engineBlockFrames = 128;
};

// What the device thread actually negotiated. Written before open() returns.
struct StreamInfo {
    bool exclusive = false;
    double deviceRate = 0.0;
    std::uint32_t deviceChannels = 0;
    std::uint32_t periodFrames = 0;
    std::string format;  // e.g. "Int24 @ 48000 Hz, 1 ch, exclusive, 3.00 ms"
};

class WasapiStream;

// USB mic (or any capture device) as a matrix input. A device thread captures in the
// device's own clock domain into a ring; the engine pulls through a DriftResampler that
// locks it to the master ASIO clock.
class WasapiInputEndpoint final : public IInputEndpoint {
public:
    [[nodiscard]] static std::shared_ptr<WasapiInputEndpoint> open(const StreamConfig& config, std::string& error);
    ~WasapiInputEndpoint() override;

    std::string_view name() const noexcept override { return label_; }
    std::uint32_t numChannels() const noexcept override { return channels_; }
    std::string channelName(std::uint32_t ch) const override;
    void pull(AudioBlock dst) noexcept override;

    [[nodiscard]] const StreamInfo& info() const noexcept;
    [[nodiscard]] const dsp::DriftResampler& resampler() const noexcept { return resampler_; }
    [[nodiscard]] bool deviceLost() const noexcept;

private:
    WasapiInputEndpoint() = default;

    std::string label_;
    std::uint32_t channels_ = 0;
    std::unique_ptr<AudioRingBuffer> ring_;
    dsp::DriftResampler resampler_;
    std::unique_ptr<WasapiStream> stream_;
};

// Matrix output to a render device (e.g. "CABLE Input" for Discord/OBS). The engine
// pushes into a ring at the master rate; the device thread pulls through a
// DriftResampler locked to the device clock.
class WasapiOutputEndpoint final : public IOutputEndpoint {
public:
    [[nodiscard]] static std::shared_ptr<WasapiOutputEndpoint> open(const StreamConfig& config, std::string& error);
    ~WasapiOutputEndpoint() override;

    std::string_view name() const noexcept override { return label_; }
    std::uint32_t numChannels() const noexcept override { return channels_; }
    std::string channelName(std::uint32_t ch) const override;
    void push(ConstAudioBlock src) noexcept override;

    [[nodiscard]] const StreamInfo& info() const noexcept;
    [[nodiscard]] const dsp::DriftResampler& resampler() const noexcept { return resampler_; }
    [[nodiscard]] bool deviceLost() const noexcept;

private:
    WasapiOutputEndpoint() = default;

    std::string label_;
    std::uint32_t channels_ = 2;
    std::unique_ptr<AudioRingBuffer> ring_;
    dsp::DriftResampler resampler_;
    std::unique_ptr<WasapiStream> stream_;
};

} // namespace afm::wasapi
