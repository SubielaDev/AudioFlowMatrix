#pragma once

#include "afm/core/AlignedBuffer.h"
#include "afm/core/AudioTypes.h"

#include <array>
#include <atomic>
#include <cstdint>

struct DenoiseState;

namespace afm::dsp {

// Expander-style noise gate with RMS detection, hysteresis and hold.
//
// All channels share one detector (the loudest channel opens the gate), so a stereo mic
// never gets its image pulled to one side. The gain moves through one-pole smoothers, so
// opening and closing never step the waveform.
class NoiseGate {
public:
    static constexpr float kMinThresholdDb = -60.0f;
    static constexpr float kMaxThresholdDb = -20.0f;
    static constexpr float kHysteresisDb = 3.0f;
    static constexpr float kRmsWindowMs = 10.0f;
    static constexpr float kAttackMs = 3.0f;
    static constexpr float kHoldMs = 180.0f;
    static constexpr float kReleaseMs = 100.0f;

    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    // `enabled` false lets the gain recover to unity smoothly instead of jumping.
    void process(AudioBlock io, bool enabled, float thresholdDb) noexcept;

    [[nodiscard]] bool isOpen() const noexcept { return open_; }
    [[nodiscard]] float gain() const noexcept { return gain_; }

private:
    float rmsCoeff_ = 0.0f;
    float attackCoeff_ = 0.0f;
    float releaseCoeff_ = 0.0f;
    std::uint32_t holdSamples_ = 0;

    float power_ = 0.0f;
    float gain_ = 1.0f;
    std::uint32_t holdLeft_ = 0;
    bool open_ = true;
    float thresholdDb_ = 1.0f;  // forces a coefficient refresh on the first block
    float openPower_ = 0.0f;
    float closePower_ = 0.0f;
};

// Microphone chain run on the capture thread: RNNoise denoiser, then the noise gate.
//
// RNNoise works on fixed 480-frame (10 ms) blocks at 48 kHz, while WASAPI delivers
// whatever its period is. Input and output both happen on the one capture thread, so a
// pair of fixed single-frame FIFOs (no ring, no atomics) re-blocks the stream: each call
// writes into the input FIFO and reads the output FIFO, and every completed input frame
// is processed into the output FIFO. RNNoise itself answers two frames late (measured: its
// analysis window plus model look-ahead), so the dry path is delayed by the same two frames
// and the chain delays audio by exactly kLatencyFrames whether denoising is on or off;
// toggling it crossfades between the denoised and the dry signal over one frame.
//
// At any other device rate the denoiser is unavailable: the chain adds no latency and only
// the gate runs.
class VoiceProcessor {
public:
    static constexpr std::uint32_t kFrame = 480;
    static constexpr std::uint32_t kMaxChannels = 2;
    static constexpr double kDenoiseRate = 48000.0;
    static constexpr std::uint32_t kDenoiseDelayFrames = 2 * kFrame;
    static constexpr std::uint32_t kLatencyFrames = kFrame + kDenoiseDelayFrames;

    VoiceProcessor() = default;
    ~VoiceProcessor();
    VoiceProcessor(const VoiceProcessor&) = delete;
    VoiceProcessor& operator=(const VoiceProcessor&) = delete;

    // Not real-time safe: allocates the RNNoise states and FIFOs.
    void prepare(std::uint32_t channels, double sampleRate);

    // Real-time. Processes io in place; channels beyond kMaxChannels pass untouched.
    void process(AudioBlock io) noexcept;

    // Any thread.
    void setDenoise(bool on) noexcept { denoise_.store(on, std::memory_order_relaxed); }
    void setGate(bool on) noexcept { gate_.store(on, std::memory_order_relaxed); }
    void setGateThresholdDb(float db) noexcept { thresholdDb_.store(db, std::memory_order_relaxed); }

    [[nodiscard]] bool denoiseAvailable() const noexcept { return denoiseAvailable_; }
    [[nodiscard]] std::uint32_t latencyFrames() const noexcept { return denoiseAvailable_ ? kLatencyFrames : 0; }
    [[nodiscard]] float voiceProbability() const noexcept { return vad_.load(std::memory_order_relaxed); }
    [[nodiscard]] bool gateOpen() const noexcept { return gateOpen_.load(std::memory_order_relaxed); }
    [[nodiscard]] float gateGain() const noexcept { return gateGain_.load(std::memory_order_relaxed); }

private:
    void processFrame() noexcept;
    void publish() noexcept;
    void releaseStates() noexcept;

    std::uint32_t channels_ = 0;
    bool denoiseAvailable_ = false;
    std::array<DenoiseState*, kMaxChannels> states_{};

    // Per channel: input being filled, the last two input frames (the dry path, aligned with
    // RNNoise's output), output being drained, and RNNoise's input and output.
    AlignedBuffer<float, kSimdAlignment> storage_;
    std::array<float*, kMaxChannels> in_{};
    std::array<float*, kMaxChannels> dry_{};
    std::array<float*, kMaxChannels> out_{};
    std::array<float*, kMaxChannels> scratch_{};
    std::array<float*, kMaxChannels> wet_{};
    std::uint32_t pos_ = 0;
    std::uint32_t dryHead_ = 0;  // which of the two dry frames is the older one
    float mix_ = 0.0f;  // 0 = dry, 1 = denoised, as applied at the end of the last frame

    NoiseGate noiseGate_;

    std::atomic<bool> denoise_{false};
    std::atomic<bool> gate_{false};
    std::atomic<float> thresholdDb_{-45.0f};

    std::atomic<float> vad_{0.0f};
    std::atomic<bool> gateOpen_{true};
    std::atomic<float> gateGain_{1.0f};
};

} // namespace afm::dsp
