#include "afm/dsp/DriftResampler.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace afm::dsp {

namespace {

// Controller tuning (error in frames, time in seconds). With ~48 k frames/s through the
// loop this gives a natural frequency of ~0.2 rad/s and damping ~0.45: drift of a few
// hundred ppm is absorbed within seconds and the ratio never steps audibly.
constexpr double kProportional = 4e-6;
constexpr double kIntegral = 1e-6;
constexpr double kMaxCorrection = 0.01;  // +-1 %
constexpr double kFillSmoothingSeconds = 0.5;

inline float hermite(const std::array<float, 4>& x, float t) noexcept {
    const float c0 = x[1];
    const float c1 = 0.5f * (x[2] - x[0]);
    const float c2 = x[0] - 2.5f * x[1] + 2.0f * x[2] - 0.5f * x[3];
    const float c3 = 0.5f * (x[3] - x[0]) + 1.5f * (x[1] - x[2]);
    return ((c3 * t + c2) * t + c1) * t + c0;
}

// Must perform exactly the same floating-point steps as the interpolation loop.
inline std::size_t inputFramesNeeded(double frac, double ratio, std::uint32_t frames) noexcept {
    std::size_t need = 0;
    for (std::uint32_t k = 0; k < frames; ++k) {
        frac += ratio;
        while (frac >= 1.0) {
            frac -= 1.0;
            ++need;
        }
    }
    return need;
}

} // namespace

void DriftResampler::prepare(std::uint32_t channels, double inputRate, double outputRate, std::size_t targetFillFrames,
                             std::uint32_t maxOutputFrames) {
    channels_ = std::min(channels, kMaxChannels);
    baseRatio_ = inputRate / outputRate;
    outputRate_ = outputRate;
    target_ = static_cast<double>(targetFillFrames);
    maxOutputFrames_ = maxOutputFrames;
    maxInputFrames_ =
        static_cast<std::size_t>(std::ceil(maxOutputFrames * baseRatio_ * (1.0 + kMaxCorrection))) + 4;

    scratch_ = AlignedBuffer<float, kSimdAlignment>(channels_ * maxInputFrames_);
    for (std::uint32_t ch = 0; ch < channels_; ++ch) {
        lanes_[ch] = scratch_.data() + ch * maxInputFrames_;
    }
    for (auto& h : history_) h.fill(0.0f);
    frac_ = 0.0;
    correction_ = 0.0;
    integral_ = 0.0;
    fillEma_ = target_;
    priming_ = true;
    ppm_.store(0.0);
    locked_.store(false);
}

void DriftResampler::outputSilence(AudioBlock dst) noexcept {
    for (std::uint32_t ch = 0; ch < dst.numChannels; ++ch) {
        std::memset(dst.channels[ch], 0, dst.numFrames * sizeof(float));
    }
}

void DriftResampler::process(AudioRingBuffer& source, AudioBlock dst) noexcept {
    const std::uint32_t frames = std::min(dst.numFrames, maxOutputFrames_);
    if (channels_ == 0 || frames == 0 || dst.numChannels == 0) {
        outputSilence(dst);
        return;
    }

    if (priming_) {
        const auto fill = static_cast<double>(source.readableFrames());
        if (fill < target_) {
            outputSilence(dst);
            return;
        }
        priming_ = false;
        fillEma_ = fill;
        locked_.store(true, std::memory_order_relaxed);
    }

    const double ratio = baseRatio_ * (1.0 + correction_);
    const std::size_t need = std::min(inputFramesNeeded(frac_, ratio, frames), maxInputFrames_);
    if (source.readableFrames() < need) {
        // Ran dry: go silent and re-prime rather than glitching sample by sample.
        priming_ = true;
        locked_.store(false, std::memory_order_relaxed);
        bump(dropouts_);
        outputSilence(dst);
        return;
    }
    source.read(AudioBlock{lanes_.data(), channels_, static_cast<std::uint32_t>(need)});

    double frac = frac_;
    for (std::uint32_t ch = 0; ch < dst.numChannels; ++ch) {
        float* out = dst.channels[ch];
        if (ch >= channels_) {
            std::memset(out, 0, dst.numFrames * sizeof(float));
            continue;
        }
        std::array<float, 4>& h = history_[ch];
        const float* in = lanes_[ch];
        std::size_t consumed = 0;
        frac = frac_;
        for (std::uint32_t k = 0; k < frames; ++k) {
            out[k] = hermite(h, static_cast<float>(frac));
            frac += ratio;
            while (frac >= 1.0) {
                frac -= 1.0;
                h[0] = h[1];
                h[1] = h[2];
                h[2] = h[3];
                h[3] = consumed < need ? in[consumed] : h[3];
                ++consumed;
            }
        }
        if (frames < dst.numFrames) {
            std::memset(out + frames, 0, (dst.numFrames - frames) * sizeof(float));
        }
    }
    frac_ = frac;

    // Overflow guard: the producer got far ahead (e.g. after a stall). Snap back.
    double fill = static_cast<double>(source.readableFrames());
    if (fill > target_ * 3.0 + static_cast<double>(maxInputFrames_) * 2.0) {
        source.discard(static_cast<std::size_t>(fill - target_));
        fill = static_cast<double>(source.readableFrames());
        fillEma_ = fill;
        bump(dropouts_);
    }
    updateController(fill, frames);
}

void DriftResampler::updateController(double fill, std::uint32_t frames) noexcept {
    const double dt = static_cast<double>(frames) / outputRate_;
    const double alpha = 1.0 - std::exp(-dt / kFillSmoothingSeconds);
    fillEma_ += alpha * (fill - fillEma_);

    const double error = fillEma_ - target_;
    integral_ += error * dt;
    const double integralLimit = kMaxCorrection / kIntegral;
    integral_ = std::clamp(integral_, -integralLimit, integralLimit);

    correction_ = std::clamp(kProportional * error + kIntegral * integral_, -kMaxCorrection, kMaxCorrection);
    ppm_.store(correction_ * 1e6, std::memory_order_relaxed);
}

} // namespace afm::dsp
