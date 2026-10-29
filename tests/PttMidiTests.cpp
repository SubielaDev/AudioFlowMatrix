// Tests for push-to-talk (state machine and the engine gate on mic -> stream) and MIDI
// message decoding / bindings. Exit code = number of failures.

#include "afm/engine/AudioEngine.h"
#include "afm/engine/PushToTalk.h"
#include "afm/engine/RingEndpoints.h"
#include "afm/midi/MidiMessage.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
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

using afm::PushToTalk;
using afm::midi::MidiBinding;
using afm::midi::MidiEvent;
using afm::midi::parseShortMessage;

constexpr std::uint32_t shortMsg(std::uint8_t status, std::uint8_t d1, std::uint8_t d2) {
    return status | static_cast<std::uint32_t>(d1) << 8 | static_cast<std::uint32_t>(d2) << 16;
}

// ---- MIDI decoding ---------------------------------------------------------------------------

void testParse() {
    MidiEvent e = parseShortMessage(shortMsg(0x90, 36, 100));
    AFM_CHECK(e.kind == MidiEvent::Kind::NoteOn && e.channel == 0 && e.number == 36 && e.value == 100);

    e = parseShortMessage(shortMsg(0x99, 40, 0));  // Note On, velocity 0 = Note Off (channel 10)
    AFM_CHECK(e.kind == MidiEvent::Kind::NoteOff && e.channel == 9 && e.number == 40);

    e = parseShortMessage(shortMsg(0x85, 60, 64));
    AFM_CHECK(e.kind == MidiEvent::Kind::NoteOff && e.channel == 5);

    e = parseShortMessage(shortMsg(0xBF, 64, 127));
    AFM_CHECK(e.kind == MidiEvent::Kind::ControlChange && e.channel == 15 && e.number == 64 && e.value == 127);

    AFM_CHECK(parseShortMessage(shortMsg(0xE0, 0, 64)).kind == MidiEvent::Kind::Other);  // pitch bend
    AFM_CHECK(parseShortMessage(shortMsg(0xC0, 5, 0)).kind == MidiEvent::Kind::Other);   // program change
    AFM_CHECK(parseShortMessage(0xF8).kind == MidiEvent::Kind::Other);                   // clock
}

void testBindings() {
    using Action = MidiBinding::Action;

    // Note pad: press on Note On, release on Note Off (either encoding); other notes/channels ignored.
    const MidiBinding pad = MidiBinding::learn(parseShortMessage(shortMsg(0x90, 36, 90)));
    AFM_CHECK(pad.type == MidiBinding::Type::Note && pad.channel == 0 && pad.number == 36);
    AFM_CHECK(pad.actionFor(parseShortMessage(shortMsg(0x90, 36, 1))) == Action::Press);
    AFM_CHECK(pad.actionFor(parseShortMessage(shortMsg(0x90, 36, 0))) == Action::Release);
    AFM_CHECK(pad.actionFor(parseShortMessage(shortMsg(0x80, 36, 0))) == Action::Release);
    AFM_CHECK(pad.actionFor(parseShortMessage(shortMsg(0x90, 37, 90))) == Action::None);
    AFM_CHECK(pad.actionFor(parseShortMessage(shortMsg(0x91, 36, 90))) == Action::None);
    AFM_CHECK(pad.actionFor(parseShortMessage(shortMsg(0xB0, 36, 127))) == Action::None);

    // CC button: >= 64 is down.
    const MidiBinding button = MidiBinding::learn(parseShortMessage(shortMsg(0xB2, 20, 127)));
    AFM_CHECK(button.type == MidiBinding::Type::ControlChange && button.channel == 2 && button.number == 20);
    AFM_CHECK(button.actionFor(parseShortMessage(shortMsg(0xB2, 20, 64))) == Action::Press);
    AFM_CHECK(button.actionFor(parseShortMessage(shortMsg(0xB2, 20, 63))) == Action::Release);
    AFM_CHECK(button.actionFor(parseShortMessage(shortMsg(0x92, 20, 127))) == Action::None);

    // Learn ignores note-offs and other messages.
    AFM_CHECK(MidiBinding::learn(parseShortMessage(shortMsg(0x80, 36, 0))).type == MidiBinding::Type::None);
    AFM_CHECK(MidiBinding::learn(parseShortMessage(shortMsg(0xE0, 0, 64))).type == MidiBinding::Type::None);
    AFM_CHECK(MidiBinding{}.actionFor(parseShortMessage(shortMsg(0x90, 0, 1))) == Action::None);

    // Round trips: atomic packing and settings.ini text.
    for (const MidiBinding& b : {pad, button, MidiBinding{}}) {
        AFM_CHECK(MidiBinding::unpack(b.pack()) == b);
        AFM_CHECK(MidiBinding::parse(b.serialize()) == b);
    }
    AFM_CHECK(button.serialize() == "cc:3:20");
    AFM_CHECK(pad.describe() == "Note 36 (ch 1)");
    for (const char* bad : {"", "note", "note:0:36", "note:17:36", "cc:1:128", "pad:1:3", "note:1"}) {
        AFM_CHECK(MidiBinding::parse(bad).type == MidiBinding::Type::None);
    }
    AFM_CHECK(afm::midi::describe(shortMsg(0x90, 36, 100)).find("Note On") == 0);
}

// ---- Push-to-talk state ------------------------------------------------------------------------

void testPttStateMachine() {
    using S = PushToTalk::Source;
    PushToTalk p;
    AFM_CHECK(!p.enabled() && p.passing());  // off by default: nothing is gated

    p.setEnabled(true);
    AFM_CHECK(!p.open() && !p.passing());

    // Hold: open while any source is down.
    p.press(S::Midi);
    AFM_CHECK(p.open());
    p.press(S::Mouse);
    p.release(S::Midi);
    AFM_CHECK(p.open());  // mouse still down
    p.release(S::Mouse);
    AFM_CHECK(!p.open());

    // Toggle: one flip per press; a repeated "on" without release counts once.
    p.setMode(PushToTalk::Mode::Toggle);
    p.press(S::Midi);
    p.press(S::Midi);
    AFM_CHECK(p.open());
    p.release(S::Midi);
    AFM_CHECK(p.open());
    p.press(S::Mouse);
    p.release(S::Mouse);
    AFM_CHECK(!p.open());
    p.press(S::Midi);
    AFM_CHECK(p.open());
    p.release(S::Midi);

    // Changing mode never leaves the mic latched open.
    p.setMode(PushToTalk::Mode::Hold);
    AFM_CHECK(!p.open());
    p.setMode(PushToTalk::Mode::Toggle);
    AFM_CHECK(!p.open());

    p.setEnabled(false);
    AFM_CHECK(p.passing());
}

// ---- Engine gate -------------------------------------------------------------------------------

struct Rig {
    afm::AudioEngine engine;
    std::shared_ptr<afm::RingInputEndpoint> mic = std::make_shared<afm::RingInputEndpoint>("Mic", 1, 8192);
    std::shared_ptr<afm::RingOutputEndpoint> stream = std::make_shared<afm::RingOutputEndpoint>("Stream", 2, 8192);
    std::shared_ptr<PushToTalk> ptt = std::make_shared<PushToTalk>();
    std::array<float, 64> hwOut0{}, hwOut1{};

    explicit Rig(bool pttEnabled) {
        ptt->setEnabled(pttEnabled);  // the app applies settings before audio starts
        engine.addInput(mic);
        engine.addOutput(stream);
        afm::AudioEngine::Config config;
        config.format = {48000.0, 64};
        config.hardwareInputs = 0;
        config.hardwareOutputs = 2;
        AFM_CHECK(engine.prepare(config));
        // Bus: inputs [mic], outputs [hw L, hw R, stream L, stream R].
        auto mix = std::make_unique<afm::dsp::MixCoefficients>();
        mix->numInputs = 1;
        mix->numOutputs = 4;
        mix->gain[0][0] = 1.0f;  // mic -> interface (monitoring): never gated
        mix->gain[2][0] = 1.0f;  // mic -> stream L
        mix->gain[3][0] = 0.5f;  // mic -> stream R at -6 dB: the gate scales the routing gain
        engine.setMix(*mix);
        std::bitset<afm::kMaxInputs> in;
        std::bitset<afm::kMaxOutputs> out;
        in.set(0);
        out.set(2);
        out.set(3);
        engine.setPtt(in, out, ptt);
    }

    // One 64-frame block of constant 1.0 from the mic; returns stream L and R and interface L.
    void block(std::vector<float>& streamL, std::vector<float>& streamR, std::vector<float>& monitor) {
        std::array<float, 64> one;
        one.fill(1.0f);
        const float* m[] = {one.data()};
        mic->ring().write(afm::ConstAudioBlock{m, 1, 64});
        float* hw[] = {hwOut0.data(), hwOut1.data()};
        engine.process(afm::ConstAudioBlock{nullptr, 0, 64}, afm::AudioBlock{hw, 2, 64});
        std::array<float, 64> l{}, r{};
        float* s[] = {l.data(), r.data()};
        stream->ring().read(afm::AudioBlock{s, 2, 64});
        streamL.insert(streamL.end(), l.begin(), l.end());
        streamR.insert(streamR.end(), r.begin(), r.end());
        monitor.insert(monitor.end(), hwOut0.begin(), hwOut0.end());
    }
};

void testEngineGate() {
    Rig rig(true);  // closed from the start: no fade, the stream never hears the mic
    std::vector<float> l, r, mon;
    for (int i = 0; i < 10; ++i) rig.block(l, r, mon);
    AFM_CHECK(*std::max_element(l.begin(), l.end()) == 0.0f);
    AFM_CHECK(*std::max_element(r.begin(), r.end()) == 0.0f);
    AFM_CHECK(mon.back() == 1.0f);  // the interface still gets the mic

    // Open: reaches the routing gains after the ~10 ms fade, without steps. The gate moves
    // in whole blocks (64 frames here), so 480 frames round up to 512.
    rig.ptt->press(PushToTalk::Source::Mouse);
    l.clear(); r.clear(); mon.clear();
    for (int i = 0; i < 10; ++i) rig.block(l, r, mon);
    float maxStep = 0.0f;
    for (std::size_t n = 1; n < l.size(); ++n) maxStep = std::max(maxStep, std::fabs(l[n] - l[n - 1]));
    AFM_CHECK(l[0] < 0.01f);
    AFM_CHECK(l[511] == 1.0f && l.back() == 1.0f);
    AFM_CHECK(r.back() == 0.5f);
    AFM_CHECK(maxStep < 0.01f);  // 1/480 per sample when the fade is linear
    std::printf("  PTT open: stream reaches full level at frame %zu, largest step %.4f\n",
                static_cast<std::size_t>(std::find(l.begin(), l.end(), 1.0f) - l.begin()), maxStep);

    // Close: back to silence within 10 ms.
    rig.ptt->release(PushToTalk::Source::Mouse);
    l.clear(); r.clear(); mon.clear();
    for (int i = 0; i < 10; ++i) rig.block(l, r, mon);
    AFM_CHECK(l[0] > 0.99f && l[511] == 0.0f && r.back() == 0.0f);
    AFM_CHECK(mon.front() == 1.0f && mon.back() == 1.0f);

    // Disabling PTT lets the mic through again.
    rig.ptt->setEnabled(false);
    l.clear(); r.clear(); mon.clear();
    for (int i = 0; i < 10; ++i) rig.block(l, r, mon);
    AFM_CHECK(l.back() == 1.0f && r.back() == 0.5f);
}

// A routing change while PTT is closed must not let the mic leak to the stream.
void testRoutingChangeWhileClosed() {
    Rig rig(true);
    std::vector<float> l, r, mon;
    for (int i = 0; i < 4; ++i) rig.block(l, r, mon);
    auto mix = std::make_unique<afm::dsp::MixCoefficients>();
    mix->numInputs = 1;
    mix->numOutputs = 4;
    mix->gain[2][0] = 2.0f;
    mix->gain[3][0] = 2.0f;
    rig.engine.setMix(*mix);
    for (int i = 0; i < 4; ++i) rig.block(l, r, mon);
    AFM_CHECK(*std::max_element(l.begin(), l.end()) == 0.0f);
    AFM_CHECK(mon.back() == 0.0f);  // the new mix removed mic -> interface, and that did apply
}

} // namespace

int main() {
    testParse();
    testBindings();
    testPttStateMachine();
    testEngineGate();
    testRoutingChangeWhileClosed();

    if (g_failures == 0) {
        std::printf("All PTT/MIDI tests passed.\n");
    }
    return g_failures;
}
