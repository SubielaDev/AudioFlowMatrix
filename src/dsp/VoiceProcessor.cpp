#include "afm/dsp/VoiceProcessor.h"

#include <rnnoise.h>

#include <algorithm>
#include <cmath>

namespace afm::dsp {

namespace {

// One-pole smoothing coefficient reaching 1 - 1/e of a step after `ms`.
float coeffForMs(double rate, float ms) noexcept {
    return static_cast<float>(1.0 - std::exp(-1000.0 / (static_cast<double>(ms) * rate)));
}

constexpr float kInt16Scale = 32768.0f;  // RNNoise expects samples in 16-bit range
constexpr std::uint32_t kBuffersPerChannel = 6;

} // namespace

// ---- NoiseGate --------------------------------------------------------------------------

void NoiseGate::prepare(double sampleRate) noexcept {
    rmsCoeff_ = coeffForMs(sampleRate, kRmsWindowMs);
    attackCoeff_ = coeffForMs(sampleRate, kAttackMs);
    releaseCoeff_ = coeffForMs(sampleRate, kReleaseMs);
    holdSamples_ = static_cast<std::uint32_t>(sampleRate * kHoldMs / 1000.0);
    reset();
}

void NoiseGate::reset() noexcept {
    power_ = 0.0f;
    gain_ = 1.0f;
    holdLeft_ = holdSamples_;
    open_ = true;
}

void NoiseGate::process(AudioBlock io, bool enabled, float thresholdDb) noexcept {
    thresholdDb = std::clamp(thresholdDb, kMinThresholdDb, kMaxThresholdDb);
    if (thresholdDb != thresholdDb_) {
        thresholdDb_ = thresholdDb;
        openPower_ = std::pow(10.0f, thresholdDb / 10.0f);
        closePower_ = std::pow(10.0f, (thresholdDb - kHysteresisDb) / 10.0f);
    }

    for (std::uint32_t f = 0; f < io.numFrames; ++f) {
        float peak = 0.0f;
        for (std::uint32_t c = 0; c < io.numChannels; ++c) {
            const float x = io.channels[c][f];
            peak = std::max(peak, x * x);
        }
        power_ += rmsCoeff_ * (peak - power_);

        if (open_) {
            if (power_ >= closePower_) {
                holdLeft_ = holdSamples_;
            } else if (holdLeft_ > 0) {
                --holdLeft_;
            } else {
                open_ = false;
            }
        } else if (power_ >= openPower_) {
            open_ = true;
            holdLeft_ = holdSamples_;
        }

        const float target = (!enabled || open_) ? 1.0f : 0.0f;
        gain_ += (target > gain_ ? attackCoeff_ : releaseCoeff_) * (target - gain_);
        if (gain_ < 1e-6f) gain_ = 0.0f;  // keep the release tail out of denormal range

        for (std::uint32_t c = 0; c < io.numChannels; ++c) io.channels[c][f] *= gain_;
    }
}

// ---- VoiceProcessor ---------------------------------------------------------------------

VoiceProcessor::~VoiceProcessor() { releaseStates(); }

void VoiceProcessor::releaseStates() noexcept {
    for (DenoiseState*& s : states_) {
        if (s != nullptr) rnnoise_destroy(s);
        s = nullptr;
    }
}

void VoiceProcessor::prepare(std::uint32_t channels, double sampleRate) {
    releaseStates();
    channels_ = std::min(channels, kMaxChannels);
    denoiseAvailable_ = sampleRate == kDenoiseRate && channels_ > 0;
    pos_ = 0;
    dryHead_ = 0;
    mix_ = denoise_.load(std::memory_order_relaxed) ? 1.0f : 0.0f;
    noiseGate_.prepare(sampleRate);

    if (denoiseAvailable_) {
        storage_ = AlignedBuffer<float, kSimdAlignment>(std::size_t{kBuffersPerChannel} * kFrame * channels_);
        for (std::uint32_t c = 0; c < channels_; ++c) {
            float* base = storage_.data() + std::size_t{kBuffersPerChannel} * kFrame * c;
            in_[c] = base;
            dry_[c] = base + kFrame;
            out_[c] = base + 3 * kFrame;
            scratch_[c] = base + 4 * kFrame;
            wet_[c] = base + 5 * kFrame;
            states_[c] = rnnoise_create(nullptr);  // built-in model
        }
    }
    publish();
}

void VoiceProcessor::process(AudioBlock io) noexcept {
    AudioBlock own{io.channels, std::min(io.numChannels, channels_), io.numFrames};
    if (own.numChannels == 0) return;

    if (!denoiseAvailable_) {
        noiseGate_.process(own, gate_.load(std::memory_order_relaxed), thresholdDb_.load(std::memory_order_relaxed));
        publish();
        return;
    }

    std::uint32_t done = 0;
    while (done < own.numFrames) {
        const std::uint32_t n = std::min(kFrame - pos_, own.numFrames - done);
        for (std::uint32_t c = 0; c < own.numChannels; ++c) {
            float* samples = own.channels[c] + done;
            float* in = in_[c] + pos_;
            float* out = out_[c] + pos_;
            for (std::uint32_t f = 0; f < n; ++f) {
                in[f] = samples[f];
                samples[f] = out[f];
            }
        }
        pos_ += n;
        done += n;
        if (pos_ == kFrame) {
            processFrame();
            pos_ = 0;
        }
    }
    publish();
}

void VoiceProcessor::processFrame() noexcept {
    float vad = 0.0f;
    for (std::uint32_t c = 0; c < channels_; ++c) {
        float* scratch = scratch_[c];
        const float* in = in_[c];
        for (std::uint32_t f = 0; f < kFrame; ++f) scratch[f] = in[f] * kInt16Scale;
        // RNNoise's output is the frame from two calls ago, denoised.
        vad = std::max(vad, rnnoise_process_frame(states_[c], wet_[c], scratch));
    }

    // Crossfade from the mix used for the last frame to the one now requested.
    const float from = mix_;
    const float to = denoise_.load(std::memory_order_relaxed) ? 1.0f : 0.0f;
    const float step = (to - from) / static_cast<float>(kFrame);
    for (std::uint32_t c = 0; c < channels_; ++c) {
        const float* wet = wet_[c];
        const float* dry = dry_[c] + dryHead_ * kFrame;
        float* out = out_[c];
        float m = from;
        for (std::uint32_t f = 0; f < kFrame; ++f) {
            m += step;
            out[f] = dry[f] + m * (wet[f] / kInt16Scale - dry[f]);
        }
    }
    mix_ = to;

    // The current input replaces the dry frame just used; it lines up two frames from now.
    for (std::uint32_t c = 0; c < channels_; ++c) std::copy_n(in_[c], kFrame, dry_[c] + dryHead_ * kFrame);
    dryHead_ ^= 1u;

    noiseGate_.process(AudioBlock{out_.data(), channels_, kFrame}, gate_.load(std::memory_order_relaxed),
                       thresholdDb_.load(std::memory_order_relaxed));
    vad_.store(vad, std::memory_order_relaxed);
}

void VoiceProcessor::publish() noexcept {
    gateOpen_.store(noiseGate_.isOpen(), std::memory_order_relaxed);
    gateGain_.store(noiseGate_.gain(), std::memory_order_relaxed);
}

} // namespace afm::dsp
