#include "afm/engine/AudioEngine.h"

#include "afm/core/Denormals.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <utility>

namespace afm {

namespace {

// ~1.4 s of history at 48 kHz / 64-frame blocks; the GUI drains it every frame.
constexpr std::size_t kTelemetryCapacity = 1024;

float peakOf(const float* samples, std::uint32_t frames) noexcept {
    float peak = 0.0f;
    for (std::uint32_t i = 0; i < frames; ++i) {
        peak = std::max(peak, std::fabs(samples[i]));
    }
    return peak;
}

} // namespace

AudioEngine::AudioEngine()
    : mixer_(std::make_unique<dsp::MatrixMixer>()),
      mix_(std::make_unique<TripleBuffer<dsp::MixCoefficients>>()),
      base_(std::make_unique<dsp::MixCoefficients>()),
      gated_(std::make_unique<dsp::MixCoefficients>()),
      telemetry_(kTelemetryCapacity) {}

AudioEngine::~AudioEngine() = default;

void AudioEngine::addInput(std::shared_ptr<IInputEndpoint> endpoint) {
    inputs_.push_back(std::move(endpoint));
}

void AudioEngine::addOutput(std::shared_ptr<IOutputEndpoint> endpoint) {
    outputs_.push_back(std::move(endpoint));
}

bool AudioEngine::prepare(const Config& config) {
    release();

    if (config.format.blockFrames == 0 || config.format.blockFrames > kMaxBlockFrames ||
        config.format.sampleRate <= 0.0) {
        return false;
    }

    std::uint32_t ins = config.hardwareInputs;
    for (const auto& ep : inputs_) {
        ins += ep->numChannels();
    }
    std::uint32_t outs = config.hardwareOutputs;
    for (const auto& ep : outputs_) {
        outs += ep->numChannels();
    }
    if (ins > kMaxInputs || outs > kMaxOutputs) {
        return false;
    }

    config_ = config;
    totalInputs_ = ins;
    totalOutputs_ = outs;

    inputBus_ = AlignedBuffer<float, kSimdAlignment>(static_cast<std::size_t>(ins) * kMaxBlockFrames);
    outputBus_ = AlignedBuffer<float, kSimdAlignment>(static_cast<std::size_t>(outs) * kMaxBlockFrames);
    for (std::uint32_t ch = 0; ch < ins; ++ch) {
        inputLanes_[ch] = inputBus_.data() + static_cast<std::size_t>(ch) * kMaxBlockFrames;
    }
    for (std::uint32_t ch = 0; ch < outs; ++ch) {
        outputLanes_[ch] = outputBus_.data() + static_cast<std::size_t>(ch) * kMaxBlockFrames;
    }

    // telemetry_ is deliberately not reset: the GUI may still be draining it.
    blockIndex_ = 0;
    prepared_ = true;
    return true;
}

void AudioEngine::release() {
    prepared_ = false;
    inputBus_ = {};
    outputBus_ = {};
    inputLanes_.fill(nullptr);
    outputLanes_.fill(nullptr);
    totalInputs_ = 0;
    totalOutputs_ = 0;
}

void AudioEngine::process(ConstAudioBlock hwIn, AudioBlock hwOut) noexcept {
    const ScopedFlushDenormals ftz;
    const auto start = std::chrono::steady_clock::now();

    if (!prepared_) {
        for (std::uint32_t ch = 0; ch < hwOut.numChannels; ++ch) {
            std::memset(hwOut.channels[ch], 0, hwOut.numFrames * sizeof(float));
        }
        return;
    }

    // Drivers may deliver a different block size than negotiated; clamp to scratch size.
    const std::uint32_t frames = std::min(hwOut.numFrames, kMaxBlockFrames);

    updateMix(frames);

    gatherInputs(hwIn, frames);
    mixer_->process(inputLanes_.data(), totalInputs_, outputLanes_.data(), totalOutputs_, frames);
    scatterOutputs(hwOut, frames);

    const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - start;
    publishTelemetry(frames, elapsed.count());
}

void AudioEngine::gatherInputs(ConstAudioBlock hwIn, std::uint32_t frames) noexcept {
    std::uint32_t ch = 0;
    for (; ch < config_.hardwareInputs; ++ch) {
        if (ch < hwIn.numChannels && hwIn.numFrames >= frames) {
            std::memcpy(inputLanes_[ch], hwIn.channels[ch], frames * sizeof(float));
        } else {
            std::memset(inputLanes_[ch], 0, frames * sizeof(float));
        }
    }
    for (const auto& ep : inputs_) {
        const std::uint32_t n = ep->numChannels();
        ep->pull(AudioBlock{inputLanes_.data() + ch, n, frames});
        ch += n;
    }
}

void AudioEngine::setMix(const dsp::MixCoefficients& coefficients) noexcept {
    mix_->back() = coefficients;
    mix_->publish();
}

void AudioEngine::setPtt(const std::bitset<kMaxInputs>& inputs, const std::bitset<kMaxOutputs>& outputs,
                         std::shared_ptr<const PushToTalk> ptt) {
    pttInputs_ = inputs;
    pttOutputs_ = outputs;
    ptt_ = std::move(ptt);
    pttGain_ = (ptt_ == nullptr || ptt_->passing()) ? 1.0f : 0.0f;  // no fade on the first block
}

void AudioEngine::updateMix(std::uint32_t frames) noexcept {
    const bool fresh = mix_->update();
    if (ptt_ == nullptr || pttInputs_.none() || pttOutputs_.none()) {
        if (fresh) mixer_->setTarget(mix_->front());
        return;
    }
    if (fresh) *base_ = mix_->front();
    if (base_->numInputs == 0) return;  // nothing published yet

    const float target = ptt_->passing() ? 1.0f : 0.0f;
    if (!fresh && pttGain_ == target) return;
    if (pttGain_ != target) {
        const auto step = static_cast<float>(frames / (kPttFadeSeconds * config_.format.sampleRate));
        pttGain_ = target > pttGain_ ? std::min(target, pttGain_ + step) : std::max(target, pttGain_ - step);
    }

    dsp::MixCoefficients& g = *gated_;
    g.numInputs = base_->numInputs;
    g.numOutputs = base_->numOutputs;
    for (std::uint32_t o = 0; o < g.numOutputs; ++o) {
        const bool pttOut = pttOutputs_.test(o);
        for (std::uint32_t i = 0; i < g.numInputs; ++i) {
            g.gain[o][i] = (pttOut && pttInputs_.test(i)) ? base_->gain[o][i] * pttGain_ : base_->gain[o][i];
        }
    }
    mixer_->setTarget(g);
}

void AudioEngine::scatterOutputs(AudioBlock hwOut, std::uint32_t frames) noexcept {
    for (std::uint32_t ch = 0; ch < hwOut.numChannels; ++ch) {
        float* dst = hwOut.channels[ch];
        if (ch < config_.hardwareOutputs) {
            std::memcpy(dst, outputLanes_[ch], frames * sizeof(float));
            std::memset(dst + frames, 0, (hwOut.numFrames - frames) * sizeof(float));
        } else {
            std::memset(dst, 0, hwOut.numFrames * sizeof(float));
        }
    }

    std::uint32_t ch = config_.hardwareOutputs;
    for (const auto& ep : outputs_) {
        const std::uint32_t n = ep->numChannels();
        ep->push(ConstAudioBlock{outputLanes_.data() + ch, n, frames});
        ch += n;
    }
}

void AudioEngine::publishTelemetry(std::uint32_t frames, double elapsedSeconds) noexcept {
    TelemetryFrame t{};
    t.blockIndex = blockIndex_++;
    t.numInputs = totalInputs_;
    t.numOutputs = totalOutputs_;
    const double period = static_cast<double>(frames) / config_.format.sampleRate;
    t.dspLoad = period > 0.0 ? static_cast<float>(elapsedSeconds / period) : 0.0f;
    for (std::uint32_t ch = 0; ch < totalInputs_; ++ch) {
        t.inputPeak[ch] = peakOf(inputLanes_[ch], frames);
    }
    for (std::uint32_t ch = 0; ch < totalOutputs_; ++ch) {
        t.outputPeak[ch] = peakOf(outputLanes_[ch], frames);
    }
    // If the GUI stalls the ring fills up and frames are dropped; the audio thread
    // never waits for it.
    (void)telemetry_.tryPush(t);
}

} // namespace afm
