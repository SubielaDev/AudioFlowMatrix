// Dependency-free test runner for the real-time core. Exit code = number of failures.

#include "afm/core/AudioRingBuffer.h"
#include "afm/core/SpscRingBuffer.h"
#include "afm/engine/AudioEngine.h"
#include "afm/engine/RingEndpoints.h"
#include "afm/engine/RoutingModel.h"
#include "afm/dsp/DriftResampler.h"
#include "afm/dsp/MatrixMixer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <sstream>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <thread>
#include <vector>

namespace {

int g_failures = 0;

#define AFM_CHECK(cond)                                                                   \
    do {                                                                                  \
        if (!(cond)) {                                                                    \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                                 \
        }                                                                                 \
    } while (0)

using afm::AudioBlock;
using afm::AudioRingBuffer;
using afm::ConstAudioBlock;
using afm::SpscRingBuffer;

void testCapacityRounding() {
    AFM_CHECK(SpscRingBuffer<int>(100).capacity() == 128);
    AFM_CHECK(SpscRingBuffer<int>(128).capacity() == 128);
    AFM_CHECK(SpscRingBuffer<int>(0).capacity() == 2);
    AFM_CHECK(AudioRingBuffer(2, 10).capacityFrames() == 64);
    AFM_CHECK(AudioRingBuffer(2, 300).capacityFrames() == 512);
}

void testWrapAround() {
    SpscRingBuffer<int> rb(8);
    const int a[] = {0, 1, 2, 3, 4};
    AFM_CHECK(rb.write(a, 5) == 5);

    int out[8] = {};
    AFM_CHECK(rb.read(out, 3) == 3);
    AFM_CHECK(out[0] == 0 && out[2] == 2);

    const int b[] = {5, 6, 7, 8, 9, 10};  // crosses the end of storage
    AFM_CHECK(rb.write(b, 6) == 6);
    AFM_CHECK(rb.readable() == 8);
    AFM_CHECK(rb.read(out, 8) == 8);
    for (int i = 0; i < 8; ++i) {
        AFM_CHECK(out[i] == i + 3);
    }
    AFM_CHECK(rb.readable() == 0);
}

void testFullAndEmpty() {
    SpscRingBuffer<int> rb(4);
    const int a[] = {0, 1, 2, 3, 4, 5};
    AFM_CHECK(rb.write(a, 6) == 4);
    AFM_CHECK(!rb.tryPush(9));
    AFM_CHECK(!rb.writeAll(a, 1));

    int v = -1;
    AFM_CHECK(rb.tryPop(v) && v == 0);
    AFM_CHECK(rb.discard(10) == 3);
    AFM_CHECK(!rb.tryPop(v));
    AFM_CHECK(!rb.readAll(&v, 1));
}

void testZeroCopy() {
    SpscRingBuffer<int> rb(8);
    const int pad[] = {0, 0, 0, 0, 0, 0};
    AFM_CHECK(rb.write(pad, 6) == 6);
    AFM_CHECK(rb.discard(6) == 6);

    auto w = rb.prepareWrite(5);  // positions 6,7 then 0,1,2
    AFM_CHECK(w.first.size() == 2 && w.second.size() == 3);
    int value = 100;
    for (int& x : w.first) x = value++;
    for (int& x : w.second) x = value++;
    rb.commitWrite(w.size());

    auto r = rb.prepareRead(5);
    AFM_CHECK(r.size() == 5);
    AFM_CHECK(r.first[0] == 100 && r.second[2] == 104);
    rb.commitRead(r.size());
    AFM_CHECK(rb.readable() == 0);
}

void testAudioUnderrunAndOverrun() {
    AudioRingBuffer ring(2, 64);
    std::array<float, 80> left{}, right{};
    left.fill(1.0f);
    right.fill(2.0f);
    const float* in[] = {left.data(), right.data()};

    AFM_CHECK(ring.write(ConstAudioBlock{in, 2, 10}) == 10);

    std::array<float, 16> outL{}, outR{};
    outL.fill(-1.0f);
    outR.fill(-1.0f);
    float* out[] = {outL.data(), outR.data()};
    AFM_CHECK(ring.read(AudioBlock{out, 2, 16}) == 10);
    AFM_CHECK(outL[9] == 1.0f && outR[9] == 2.0f);
    AFM_CHECK(outL[10] == 0.0f && outR[15] == 0.0f);
    AFM_CHECK(ring.underruns() == 6);

    AFM_CHECK(ring.write(ConstAudioBlock{in, 2, 80}) == 64);
    AFM_CHECK(ring.overruns() == 16);
    AFM_CHECK(ring.fillLevelFrames() == 64);
}

void stressSpsc() {
    constexpr std::uint32_t kCount = 5'000'000;
    SpscRingBuffer<std::uint32_t> rb(1024);
    bool ordered = true;

    std::thread producer([&] {
        std::uint32_t chunk[64];
        std::uint32_t next = 0;
        while (next < kCount) {
            const std::uint32_t n = std::min<std::uint32_t>(next % 61 + 1, kCount - next);
            for (std::uint32_t i = 0; i < n; ++i) chunk[i] = next + i;
            std::uint32_t done = 0;
            while (done < n) {
                const auto w = static_cast<std::uint32_t>(rb.write(chunk + done, n - done));
                if (w == 0) std::this_thread::yield();
                done += w;
            }
            next += n;
        }
    });

    std::uint32_t chunk[64];
    std::uint32_t expected = 0;
    while (expected < kCount) {
        const auto n = static_cast<std::uint32_t>(rb.read(chunk, expected % 53 + 1));
        if (n == 0) {
            std::this_thread::yield();
            continue;
        }
        for (std::uint32_t i = 0; i < n; ++i) {
            ordered = ordered && chunk[i] == expected + i;
        }
        expected += n;
    }
    producer.join();
    AFM_CHECK(ordered);
    AFM_CHECK(rb.readable() == 0);
}

void stressAudioThreeChannels() {
    // 3 channels is deliberately not a power of two.
    constexpr std::uint32_t kChannels = 3;
    constexpr std::uint32_t kFrames = 2'000'000;
    AudioRingBuffer ring(kChannels, 256);
    bool ordered = true;

    auto sampleValue = [](std::uint32_t frame, std::uint32_t ch) {
        return static_cast<float>(frame & 0xFFFFF) + 0.25f * static_cast<float>(ch);
    };

    std::thread producer([&] {
        std::array<std::array<float, 97>, kChannels> lanes{};
        const float* in[kChannels] = {lanes[0].data(), lanes[1].data(), lanes[2].data()};
        std::uint32_t next = 0;
        while (next < kFrames) {
            const std::uint32_t want = std::min<std::uint32_t>(next % 97 + 1, kFrames - next);
            const auto n = std::min<std::uint32_t>(want, static_cast<std::uint32_t>(ring.writableFrames()));
            if (n == 0) {
                std::this_thread::yield();
                continue;
            }
            for (std::uint32_t ch = 0; ch < kChannels; ++ch)
                for (std::uint32_t i = 0; i < n; ++i) lanes[ch][i] = sampleValue(next + i, ch);
            ring.write(ConstAudioBlock{in, kChannels, n});
            next += n;
        }
    });

    std::array<std::array<float, 89>, kChannels> lanes{};
    float* out[kChannels] = {lanes[0].data(), lanes[1].data(), lanes[2].data()};
    std::uint32_t expected = 0;
    while (expected < kFrames) {
        const auto n = std::min<std::uint32_t>(expected % 89 + 1, static_cast<std::uint32_t>(ring.readableFrames()));
        if (n == 0) {
            std::this_thread::yield();
            continue;
        }
        ring.read(AudioBlock{out, kChannels, n});
        for (std::uint32_t ch = 0; ch < kChannels; ++ch)
            for (std::uint32_t i = 0; i < n; ++i) ordered = ordered && lanes[ch][i] == sampleValue(expected + i, ch);
        expected += n;
    }
    producer.join();
    AFM_CHECK(ordered);
    AFM_CHECK(ring.overruns() == 0);
    AFM_CHECK(ring.underruns() == 0);
}

void testEngineIdentityRouting() {
    afm::AudioEngine engine;
    auto mic = std::make_shared<afm::RingInputEndpoint>("USB Mic", 1, 256);
    engine.addInput(mic);

    afm::AudioEngine::Config config;
    config.format = {48000.0, 64};
    config.hardwareInputs = 2;
    config.hardwareOutputs = 2;
    AFM_CHECK(engine.prepare(config));
    AFM_CHECK(engine.totalInputs() == 3);
    AFM_CHECK(engine.totalOutputs() == 2);

    // Identity routing: hardware in 1/2 -> hardware out 1/2.
    auto mix = std::make_unique<afm::dsp::MixCoefficients>();
    mix->numInputs = 3;
    mix->numOutputs = 2;
    mix->gain[0][0] = 1.0f;
    mix->gain[1][1] = 1.0f;
    engine.setMix(*mix);

    std::array<float, 64> in0{}, in1{}, out0{}, out1{};
    in0.fill(0.5f);
    in1.fill(-0.25f);
    const float* in[] = {in0.data(), in1.data()};
    float* out[] = {out0.data(), out1.data()};

    // First block ramps 0 -> 1 (click-free); it must end exactly on the target gain.
    engine.process(ConstAudioBlock{in, 2, 64}, AudioBlock{out, 2, 64});
    AFM_CHECK(out0[0] > 0.0f && out0[0] < 0.05f);
    AFM_CHECK(out0[63] == 0.5f && out1[63] == -0.25f);

    engine.process(ConstAudioBlock{in, 2, 64}, AudioBlock{out, 2, 64});
    AFM_CHECK(out0[0] == 0.5f && out0[63] == 0.5f);
    AFM_CHECK(out1[0] == -0.25f && out1[63] == -0.25f);

    afm::TelemetryFrame t;
    AFM_CHECK(engine.popTelemetry(t) && t.blockIndex == 0);
    AFM_CHECK(engine.popTelemetry(t) && t.blockIndex == 1 && t.numInputs == 3);
    AFM_CHECK(t.inputPeak[0] == 0.5f && t.outputPeak[1] == 0.25f);
    AFM_CHECK(t.inputPeak[2] == 0.0f);   // mic ring was empty -> silence
    AFM_CHECK(mic->ring().underruns() == 128);
}

void testMatrixMixerAgainstReference() {
    constexpr std::uint32_t kIn = 11, kOut = 7, kFrames = 77;  // odd sizes exercise every tail path
    std::vector<std::vector<float>> inData(kIn, std::vector<float>(kFrames));
    std::vector<std::vector<float>> outData(kOut, std::vector<float>(kFrames));
    std::vector<const float*> in;
    std::vector<float*> out;
    std::uint32_t seed = 12345;
    auto rnd = [&] { seed = seed * 1664525u + 1013904223u; return static_cast<float>(seed >> 8) / 16777216.0f * 2.0f - 1.0f; };
    for (auto& lane : inData) { for (float& x : lane) x = rnd(); in.push_back(lane.data()); }
    for (auto& lane : outData) out.push_back(lane.data());

    auto c = std::make_unique<afm::dsp::MixCoefficients>();
    c->numInputs = kIn;
    c->numOutputs = kOut;
    for (std::uint32_t o = 0; o < kOut; ++o)
        for (std::uint32_t i = 0; i < kIn; ++i)
            c->gain[o][i] = (o + i) % 3 == 0 ? rnd() : 0.0f;

    auto mixer = std::make_unique<afm::dsp::MatrixMixer>();
    mixer->setTarget(*c);
    mixer->process(in.data(), kIn, out.data(), kOut, kFrames);  // ramp block
    // Ramp ends exactly on target.
    for (std::uint32_t o = 0; o < kOut; ++o) {
        double ref = 0.0;
        for (std::uint32_t i = 0; i < kIn; ++i) ref += static_cast<double>(c->gain[o][i]) * inData[i][kFrames - 1];
        AFM_CHECK(std::fabs(outData[o][kFrames - 1] - ref) < 1e-4);
    }
    mixer->process(in.data(), kIn, out.data(), kOut, kFrames);  // steady block
    double maxErr = 0.0;
    for (std::uint32_t o = 0; o < kOut; ++o)
        for (std::uint32_t f = 0; f < kFrames; ++f) {
            double ref = 0.0;
            for (std::uint32_t i = 0; i < kIn; ++i) ref += static_cast<double>(c->gain[o][i]) * inData[i][f];
            maxErr = std::max(maxErr, std::fabs(outData[o][f] - ref));
        }
    AFM_CHECK(maxErr < 1e-5);

    // Layout mismatch -> silence, never garbage.
    mixer->process(in.data(), kIn - 1, out.data(), kOut, kFrames);
    AFM_CHECK(outData[0][0] == 0.0f && outData[kOut - 1][kFrames - 1] == 0.0f);
    std::printf("  mixer kernel: %s\n", afm::dsp::MatrixMixer::usingAvx2() ? "AVX2/FMA" : "scalar");
}

void testDriftResamplerLocks() {
    // Mic crystal runs +200 ppm fast, delivering 144-frame packets (3 ms); the engine
    // pulls 64-frame blocks at exactly 48 kHz. Simulate 120 s of both clocks.
    constexpr double kEngineRate = 48000.0;
    constexpr double kDrift = 200e-6;
    constexpr double kMicRate = kEngineRate * (1.0 + kDrift);
    constexpr std::uint32_t kPacket = 144, kBlock = 64;

    AudioRingBuffer ring(1, 8192);
    afm::dsp::DriftResampler rs;
    rs.prepare(1, kEngineRate, kEngineRate, kPacket + kBlock + kPacket / 2 + 16, kBlock);

    std::array<float, kPacket> packet{};
    packet.fill(0.5f);
    const float* pin[] = {packet.data()};
    std::array<float, kBlock> block{};
    float* pout[] = {block.data()};

    double tMic = 0.0, tEngine = 0.0;
    bool steadyOk = true;
    std::uint64_t dropoutsAfterLock = 0;
    while (tEngine < 120.0) {
        if (tMic <= tEngine) {
            ring.write(ConstAudioBlock{pin, 1, kPacket});
            tMic += kPacket / kMicRate;
        } else {
            rs.process(ring, AudioBlock{pout, 1, kBlock});
            tEngine += kBlock / kEngineRate;
            if (tEngine > 2.0) {
                for (float x : block) steadyOk = steadyOk && std::fabs(x - 0.5f) < 1e-5f;
                if (tEngine < 2.01) dropoutsAfterLock = rs.dropouts();
            }
        }
    }
    std::printf("  resampler: correction %+.1f ppm (true drift +200), dropouts %llu\n", rs.correctionPpm(),
                static_cast<unsigned long long>(rs.dropouts()));
    AFM_CHECK(rs.locked());
    AFM_CHECK(std::fabs(rs.correctionPpm() - 200.0) < 20.0);
    AFM_CHECK(rs.dropouts() == dropoutsAfterLock);
    AFM_CHECK(steadyOk);
}

void testRoutingModel() {
    afm::RoutingModel model;
    model.setLayout({"Deck A", "Mic"}, {"Out L", "Out R"}, {afm::PanSide::Left, afm::PanSide::Right});
    model.at(0, 0).routed = true;                       // Deck A -> L at 0 dB
    model.at(1, 1).routed = true;                       // Mic -> R at -6 dB, panned half left
    model.at(1, 1).gainDb = -6.0f;
    model.at(1, 1).pan = -0.5f;
    model.output(0).gainDb = -60.0f;                    // fader at bottom = -inf

    auto c = std::make_unique<afm::dsp::MixCoefficients>();
    model.compile(*c);
    AFM_CHECK(c->numInputs == 2 && c->numOutputs == 2);
    AFM_CHECK(c->gain[0][0] == 0.0f);
    AFM_CHECK(std::fabs(c->gain[1][1] - 0.5012f * 0.5f) < 1e-3f);

    // Settings follow channel names across layout changes and survive save/load.
    std::stringstream ss;
    model.save(ss);
    afm::RoutingModel loaded;
    loaded.load(ss);
    loaded.setLayout({"Mic", "Deck A", "New"}, {"Out R"}, {afm::PanSide::Mono});
    AFM_CHECK(loaded.at(0, 0).routed && loaded.at(0, 0).gainDb == -6.0f);
    AFM_CHECK(!loaded.at(0, 1).routed && !loaded.at(0, 2).routed);
}

} // namespace

int main() {
    testCapacityRounding();
    testWrapAround();
    testFullAndEmpty();
    testZeroCopy();
    testAudioUnderrunAndOverrun();
    stressSpsc();
    stressAudioThreeChannels();
    testEngineIdentityRouting();
    testMatrixMixerAgainstReference();
    testDriftResamplerLocks();
    testRoutingModel();

    if (g_failures == 0) {
        std::printf("All tests passed.\n");
    }
    return g_failures;
}
