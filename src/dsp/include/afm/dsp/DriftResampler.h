#pragma once

#include "afm/core/AlignedBuffer.h"
#include "afm/core/AudioRingBuffer.h"
#include "afm/core/AudioTypes.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace afm::dsp {

// Adaptive resampler that slaves a stream from one clock domain to another.
//
// It sits on the *consumer* side of an AudioRingBuffer filled by a device running on
// its own crystal (e.g. a USB mic at "48 kHz" that is really 48 001.7 Hz). Every call
// pulls exactly dst.numFrames output frames and consumes however many input frames the
// current ratio requires, interpolating with a 4-point cubic Hermite kernel (2 samples
// of look-ahead, ~0.04 ms at 48 kHz).
//
// The ratio = nominal (inRate / outRate) * (1 + correction), where `correction` comes
// from a PI controller that keeps the ring's (smoothed) fill level at a target. That
// absorbs clock drift without ever dropping or repeating samples. If the ring runs dry
// the resampler outputs silence and re-primes up to the target before resuming.
class DriftResampler {
public:
    static constexpr std::uint32_t kMaxChannels = 8;

    DriftResampler() = default;
    DriftResampler(const DriftResampler&) = delete;
    DriftResampler& operator=(const DriftResampler&) = delete;

    // Not real-time safe: allocates scratch for up to `maxOutputFrames` per call.
    // `targetFillFrames` is in input (ring) frames.
    void prepare(std::uint32_t channels, double inputRate, double outputRate, std::size_t targetFillFrames,
                 std::uint32_t maxOutputFrames);

    // Real-time. Fills every channel of dst (extra channels get silence).
    void process(AudioRingBuffer& source, AudioBlock dst) noexcept;

    // Any thread.
    [[nodiscard]] double correctionPpm() const noexcept { return ppm_.load(std::memory_order_relaxed); }
    [[nodiscard]] bool locked() const noexcept { return locked_.load(std::memory_order_relaxed); }
    [[nodiscard]] std::uint64_t dropouts() const noexcept { return dropouts_.load(std::memory_order_relaxed); }
    [[nodiscard]] double targetFillFrames() const noexcept { return target_; }

private:
    void outputSilence(AudioBlock dst) noexcept;
    void updateController(double fill, std::uint32_t frames) noexcept;
    void bump(std::atomic<std::uint64_t>& counter) noexcept {
        counter.store(counter.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
    }

    std::uint32_t channels_ = 0;
    double baseRatio_ = 1.0;
    double outputRate_ = 48000.0;
    double target_ = 0.0;
    std::size_t maxInputFrames_ = 0;
    std::uint32_t maxOutputFrames_ = 0;

    AlignedBuffer<float, kSimdAlignment> scratch_;
    std::array<float*, kMaxChannels> lanes_{};
    std::array<std::array<float, 4>, kMaxChannels> history_{};

    double frac_ = 0.0;
    double correction_ = 0.0;
    double integral_ = 0.0;
    double fillEma_ = 0.0;
    bool priming_ = true;

    std::atomic<double> ppm_{0.0};
    std::atomic<bool> locked_{false};
    std::atomic<std::uint64_t> dropouts_{0};
};

} // namespace afm::dsp
