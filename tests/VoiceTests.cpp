// Tests for the mic chain (RNNoise denoiser + noise gate). Exit code = number of failures.

#include "afm/dsp/VoiceProcessor.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <random>
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
using afm::dsp::NoiseGate;
using afm::dsp::VoiceProcessor;

constexpr double kRate = 48000.0;
constexpr float kPi = 3.14159265f;

// Voiced-speech stand-in: a gliding 120-180 Hz harmonic series with a 4 Hz syllable
// envelope, plus light noise.
std::vector<float> speechLike(std::size_t frames, std::uint32_t seed) {
    std::mt19937 rng(seed);
    std::normal_distribution<float> noise(0.0f, 0.003f);
    std::vector<float> x(frames);
    float phase = 0.0f;
    for (std::size_t n = 0; n < frames; ++n) {
        const float t = static_cast<float>(n) / static_cast<float>(kRate);
        const float f0 = 150.0f + 30.0f * std::sin(2.0f * kPi * 0.7f * t);
        phase += 2.0f * kPi * f0 / static_cast<float>(kRate);
        float v = 0.0f;
        for (int h = 1; h <= 20; ++h) v += std::sin(phase * static_cast<float>(h)) / static_cast<float>(h);
        const float env = 0.5f + 0.5f * std::sin(2.0f * kPi * 4.0f * t);
        x[n] = 0.08f * env * v + noise(rng);
    }
    return x;
}

std::vector<float> whiteNoise(std::size_t frames, float rms, std::uint32_t seed) {
    std::mt19937 rng(seed);
    std::normal_distribution<float> d(0.0f, rms);
    std::vector<float> x(frames);
    for (float& v : x) v = d(rng);
    return x;
}

// Runs a mono signal through the processor in blocks whose sizes cycle through `sizes`.
std::vector<float> run(VoiceProcessor& vp, std::vector<float> x, const std::vector<std::uint32_t>& sizes) {
    std::size_t pos = 0;
    for (std::size_t i = 0; pos < x.size(); ++i) {
        const auto n = static_cast<std::uint32_t>(std::min<std::size_t>(sizes[i % sizes.size()], x.size() - pos));
        float* ch[] = {x.data() + pos};
        vp.process(AudioBlock{ch, 1, n});
        pos += n;
    }
    return x;
}

double rms(const std::vector<float>& x, std::size_t from, std::size_t to) {
    double sum = 0.0;
    for (std::size_t i = from; i < to; ++i) sum += static_cast<double>(x[i]) * x[i];
    return std::sqrt(sum / static_cast<double>(to - from));
}

double toDb(double v) { return 20.0 * std::log10(std::max(v, 1e-12)); }

void testBlockSizeInvariance() {
    const auto input = speechLike(48000 * 2, 1);
    VoiceProcessor a, b;
    for (VoiceProcessor* vp : {&a, &b}) {
        vp->setDenoise(true);
        vp->setGate(true);
        vp->setGateThresholdDb(-40.0f);
        vp->prepare(1, kRate);
    }
    const auto fixed = run(a, input, {480});
    const auto ragged = run(b, input, {1, 37, 144, 441, 480, 481, 1000, 7});
    AFM_CHECK(fixed == ragged);
}

void testDryPathIsPureDelay() {
    const auto input = whiteNoise(48000, 0.1f, 2);
    VoiceProcessor vp;
    vp.prepare(1, kRate);
    AFM_CHECK(vp.latencyFrames() == VoiceProcessor::kLatencyFrames);
    const auto out = run(vp, input, {144});
    bool exact = true;
    for (std::size_t n = 0; n < out.size(); ++n) {
        const float expected = n >= VoiceProcessor::kLatencyFrames ? input[n - VoiceProcessor::kLatencyFrames] : 0.0f;
        exact &= out[n] == expected;
    }
    AFM_CHECK(exact);
}

// The denoised path must line up with the dry path, or toggling would comb-filter.
void testDenoisedPathAlignment() {
    const auto input = speechLike(48000 * 3, 3);
    VoiceProcessor vp;
    vp.setDenoise(true);
    vp.prepare(1, kRate);
    const auto out = run(vp, input, {256});

    const std::size_t from = 48000, to = input.size();
    std::size_t bestLag = 0;
    double best = -1.0;
    for (std::size_t lag = 0; lag <= 2000; ++lag) {
        double c = 0.0;
        for (std::size_t n = from; n < to; ++n) c += static_cast<double>(out[n]) * input[n - lag];
        if (c > best) {
            best = c;
            bestLag = lag;
        }
    }
    std::printf("  denoised path: correlation peak at %zu frames (expected %u), output %.1f dB vs input %.1f dB\n",
                bestLag, VoiceProcessor::kLatencyFrames, toDb(rms(out, from, to)), toDb(rms(input, from, to)));
    // The measured peak sits one sample early (1439); allow +-1.
    AFM_CHECK(bestLag + 1 >= VoiceProcessor::kLatencyFrames && bestLag <= VoiceProcessor::kLatencyFrames + 1);
}

void testDenoiseRemovesNoise() {
    const auto input = whiteNoise(48000 * 3, 0.03f, 4);  // about -30 dBFS
    VoiceProcessor vp;
    vp.setDenoise(true);
    vp.prepare(1, kRate);
    const auto out = run(vp, input, {480});
    const double reduction = toDb(rms(input, 48000, input.size())) - toDb(rms(out, 48000, out.size()));
    std::printf("  white noise reduced by %.1f dB, voice probability %.2f\n", reduction, vp.voiceProbability());
    AFM_CHECK(reduction > 15.0);
}

// Toggling denoise mid-stream crossfades over one frame instead of switching hard.
void testToggleIsSmooth() {
    std::vector<float> input(48000);
    for (std::size_t n = 0; n < input.size(); ++n) input[n] = 0.2f * std::sin(2.0f * kPi * 200.0f * n / kRate);
    VoiceProcessor vp;
    vp.prepare(1, kRate);
    std::vector<float> x = input;
    float maxStep = 0.0f;
    float prev = 0.0f;
    for (std::size_t pos = 0; pos < x.size(); pos += 480) {
        if (pos == 9600) vp.setDenoise(true);
        if (pos == 28800) vp.setDenoise(false);
        float* ch[] = {x.data() + pos};
        vp.process(AudioBlock{ch, 1, 480});
        for (std::size_t n = pos; n < pos + 480; ++n) {
            if (n > VoiceProcessor::kLatencyFrames) maxStep = std::max(maxStep, std::fabs(x[n] - prev));
            prev = x[n];
        }
    }
    // A 200 Hz sine at 0.2 moves at most 0.0053 per sample; allow the fade some slack.
    std::printf("  largest sample step across denoise toggles: %.4f\n", maxStep);
    AFM_CHECK(maxStep < 0.02f);
}

void testGateTiming() {
    NoiseGate gate;
    gate.prepare(kRate);
    const auto ms = [](double v) { return static_cast<std::size_t>(v * kRate / 1000.0); };

    std::vector<float> level;
    level.insert(level.end(), ms(300), 0.1f);    // -20 dBFS: open
    level.insert(level.end(), ms(1000), 0.001f); // -60 dBFS: hold, then release
    level.insert(level.end(), ms(200), 0.1f);    // reopen

    std::vector<float> gain(level.size());
    for (std::size_t n = 0; n < level.size(); ++n) {
        float x = level[n];
        float* ch[] = {&x};
        gate.process(AudioBlock{ch, 1, 1}, true, -40.0f);
        gain[n] = x / level[n];
    }
    float maxStep = 0.0f;
    for (std::size_t n = 1; n < gain.size(); ++n) maxStep = std::max(maxStep, std::fabs(gain[n] - gain[n - 1]));

    AFM_CHECK(gain[ms(290)] > 0.999f);           // open on signal
    AFM_CHECK(gain[ms(300 + 150)] > 0.99f);      // still held 150 ms after it stops
    AFM_CHECK(gain[ms(300 + 400)] < 0.3f);       // releasing once the hold ends
    AFM_CHECK(gain[ms(300 + 900)] < 0.01f);      // closed after hold + release
    AFM_CHECK(gain[ms(1300 + 15)] > 0.9f);       // fast attack on reopen
    AFM_CHECK(maxStep < 0.01f);                  // no steps -> no clicks
    std::printf("  gate: held %.3f at +150 ms, %.3f at +400 ms, %.4f at +900 ms, %.3f 15 ms after reopening, "
                "max step %.4f\n",
                gain[ms(450)], gain[ms(700)], gain[ms(1200)], gain[ms(1315)], maxStep);

    // Hysteresis: once open, a level 1.5 dB under the threshold keeps it open.
    NoiseGate h;
    h.prepare(kRate);
    const float under = std::pow(10.0f, (-40.0f - 1.5f) / 20.0f);
    for (std::size_t n = 0; n < ms(1000); ++n) {
        float x = n < ms(100) ? 0.1f : under;
        float* ch[] = {&x};
        h.process(AudioBlock{ch, 1, 1}, true, -40.0f);
    }
    AFM_CHECK(h.isOpen());

    // Disabling the gate lets the gain recover to unity.
    for (std::size_t n = 0; n < ms(500); ++n) {
        float x = 0.0f;
        float* ch[] = {&x};
        gate.process(AudioBlock{ch, 1, 1}, n < ms(400), -40.0f);
    }
    AFM_CHECK(gate.gain() > 0.99f);
}

void testOtherRatesSkipDenoise() {
    const auto input = whiteNoise(44100, 0.1f, 5);
    VoiceProcessor vp;
    vp.setDenoise(true);
    vp.prepare(1, 44100.0);
    AFM_CHECK(!vp.denoiseAvailable());
    AFM_CHECK(vp.latencyFrames() == 0);
    AFM_CHECK(run(vp, input, {441}) == input);  // gate off: untouched, no delay
}

void benchmark() {
    constexpr std::size_t kSeconds = 10;
    auto left = speechLike(48000 * kSeconds, 6);
    auto right = speechLike(48000 * kSeconds, 7);
    VoiceProcessor vp;
    vp.setDenoise(true);
    vp.setGate(true);
    vp.prepare(2, kRate);
    const auto t0 = std::chrono::steady_clock::now();
    for (std::size_t pos = 0; pos < left.size(); pos += 144) {
        float* ch[] = {left.data() + pos, right.data() + pos};
        vp.process(AudioBlock{ch, 2, 144});
    }
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("  stereo denoise + gate: %.2f%% of one core in real time\n", 100.0 * secs / kSeconds);
    AFM_CHECK(secs / kSeconds < 0.25);
}

} // namespace

int main() {
    testBlockSizeInvariance();
    testDryPathIsPureDelay();
    testDenoisedPathAlignment();
    testDenoiseRemovesNoise();
    testToggleIsSmooth();
    testGateTiming();
    testOtherRatesSkipDenoise();
    benchmark();

    if (g_failures == 0) {
        std::printf("All voice tests passed.\n");
    }
    return g_failures;
}
