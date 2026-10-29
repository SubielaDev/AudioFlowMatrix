#pragma once

#include "afm/core/AlignedBuffer.h"
#include "afm/core/AudioTypes.h"
#include "afm/core/Config.h"
#include "afm/core/SpscRingBuffer.h"
#include "afm/core/TripleBuffer.h"
#include "afm/dsp/MatrixMixer.h"
#include "afm/engine/IAudioEndpoint.h"
#include "afm/engine/PushToTalk.h"
#include "afm/engine/Telemetry.h"

#include <array>
#include <bitset>
#include <cstdint>
#include <memory>
#include <vector>

namespace afm {

// Core of AudioFlow Matrix. The master ASIO driver owns the clock and calls process()
// from its bufferSwitch callback; everything else is slaved to it.
//
//   matrix inputs  = [ master driver inputs | input endpoints in registration order ]
//   matrix outputs = [ master driver outputs | output endpoints in registration order ]
//
// Threading:
//   - Control thread: addInput/addOutput/prepare/release, only while the driver is stopped.
//                     setMix() at any time (single writer).
//   - Audio thread:   process(). noexcept, no allocation, no locks, no blocking syscalls.
//   - GUI thread:     popTelemetry(). Single consumer.
class AudioEngine {
public:
    struct Config {
        StreamFormat format;
        std::uint32_t hardwareInputs = 0;
        std::uint32_t hardwareOutputs = 0;
    };

    AudioEngine();
    ~AudioEngine();

    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    // ---- Control thread -------------------------------------------------------------

    void addInput(std::shared_ptr<IInputEndpoint> endpoint);
    void addOutput(std::shared_ptr<IOutputEndpoint> endpoint);
    [[nodiscard]] const std::vector<std::shared_ptr<IInputEndpoint>>& inputEndpoints() const { return inputs_; }
    [[nodiscard]] const std::vector<std::shared_ptr<IOutputEndpoint>>& outputEndpoints() const { return outputs_; }

    // Lays out the bus and allocates all scratch memory. Returns false if the
    // configuration exceeds kMaxInputs / kMaxOutputs / kMaxBlockFrames.
    [[nodiscard]] bool prepare(const Config& config);
    void release();

    [[nodiscard]] const Config& config() const noexcept { return config_; }
    [[nodiscard]] std::uint32_t totalInputs() const noexcept { return totalInputs_; }
    [[nodiscard]] std::uint32_t totalOutputs() const noexcept { return totalOutputs_; }

    // Publishes new routing coefficients; the audio thread picks them up at the next
    // block and ramps to them. Wait-free.
    void setMix(const dsp::MixCoefficients& coefficients) noexcept;

    // ---- Push-to-talk ---------------------------------------------------------------
    //
    // Crosspoints from the PTT inputs to the PTT outputs (mic -> stream) follow the shared
    // PushToTalk gate on top of their routing gain. The gate fades over kPttFadeSeconds,
    // in per-block steps the mixer ramps between, so opening and closing never click.

    static constexpr double kPttFadeSeconds = 0.010;

    // Control thread, after prepare() and before the driver starts. `ptt` is read by the
    // audio thread every block; it is shared so the GUI and MIDI threads can outlive us.
    void setPtt(const std::bitset<kMaxInputs>& inputs, const std::bitset<kMaxOutputs>& outputs,
                std::shared_ptr<const PushToTalk> ptt);

    // ---- Audio thread ---------------------------------------------------------------

    // hwIn/hwOut are the master driver's buffers, already converted to planar float.
    void process(ConstAudioBlock hwIn, AudioBlock hwOut) noexcept;

    // ---- GUI thread -----------------------------------------------------------------

    [[nodiscard]] bool popTelemetry(TelemetryFrame& frame) noexcept { return telemetry_.tryPop(frame); }

private:
    void gatherInputs(ConstAudioBlock hwIn, std::uint32_t frames) noexcept;
    void scatterOutputs(AudioBlock hwOut, std::uint32_t frames) noexcept;
    void publishTelemetry(std::uint32_t frames, double elapsedSeconds) noexcept;
    void updateMix(std::uint32_t frames) noexcept;

    Config config_{};
    bool prepared_ = false;

    std::vector<std::shared_ptr<IInputEndpoint>> inputs_;
    std::vector<std::shared_ptr<IOutputEndpoint>> outputs_;
    std::uint32_t totalInputs_ = 0;
    std::uint32_t totalOutputs_ = 0;

    // Planar bus scratch: one kMaxBlockFrames lane per channel.
    AlignedBuffer<float, kSimdAlignment> inputBus_;
    AlignedBuffer<float, kSimdAlignment> outputBus_;
    std::array<float*, kMaxInputs> inputLanes_{};
    std::array<float*, kMaxOutputs> outputLanes_{};

    std::unique_ptr<dsp::MatrixMixer> mixer_;
    std::unique_ptr<TripleBuffer<dsp::MixCoefficients>> mix_;

    // Latest published coefficients, and the copy with the PTT gate applied (audio thread).
    std::unique_ptr<dsp::MixCoefficients> base_;
    std::unique_ptr<dsp::MixCoefficients> gated_;
    std::bitset<kMaxInputs> pttInputs_;
    std::bitset<kMaxOutputs> pttOutputs_;
    std::shared_ptr<const PushToTalk> ptt_;
    float pttGain_ = 1.0f;

    SpscRingBuffer<TelemetryFrame> telemetry_;
    std::uint64_t blockIndex_ = 0;
};

} // namespace afm
