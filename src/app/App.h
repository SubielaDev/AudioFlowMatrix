#pragma once

#include "AsioHost.h"
#include "Log.h"
#include "VirtualAsioServer.h"
#include "afm/core/SpscRingBuffer.h"
#include "afm/engine/AudioEngine.h"
#include "afm/engine/PushToTalk.h"
#include "afm/engine/RoutingModel.h"
#include "afm/platform/Win.h"
#include "afm/midi/MidiInput.h"
#include "afm/midi/MidiMessage.h"
#include "afm/wasapi/WasapiEndpoints.h"

#include <atomic>
#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace afm::app {

struct Settings {
    std::string driverName;
    double sampleRate = 0.0;  // 0 = keep the rate the driver is already running at
    bool micEnabled = false;
    std::wstring micId;
    bool micExclusive = true;
    bool micDenoise = false;
    bool micGate = false;
    float micGateThresholdDb = -45.0f;
    bool streamEnabled = false;
    std::wstring streamId;
    bool streamExclusive = false;
    bool autoStart = true;
    bool pttEnabled = false;   // push-to-talk on mic -> stream (optional, off by default)
    bool pttToggle = false;    // false: hold to talk, true: each press toggles
    std::string midiDevice;    // MIDI input port name; empty = none
    std::string pttMidi = "none";  // MidiBinding::serialize()
};

// Group of consecutive bus channels that belong to one device, for display.
struct ChannelGroup {
    std::string name;
    std::uint32_t begin = 0;
    std::uint32_t count = 0;
};

struct Meter {
    float levelDb = -90.0f;
    float holdDb = -90.0f;
    double holdTime = 0.0;
};

// Owns the audio graph and draws the UI. Lives on the GUI thread (COM STA), which is
// also the thread that talks to the master ASIO driver.
class App {
public:
    explicit App(HWND window);
    ~App();

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    // One GUI frame: housekeeping + ImGui drawing.
    void frame();

private:
    // ---- Audio lifecycle -----------------------------------------------------------
    void refreshDevices();
    bool startAudio();
    void stopAudio();
    void restartAudio();
    void rebuildLayout();
    void applyDefaultRouting();
    void publishMix();
    void pollTelemetry();

    // ---- Persistence ---------------------------------------------------------------
    [[nodiscard]] std::wstring settingsPath() const;
    bool loadSettings();  // false on first run (no settings file yet)
    void saveSettings() const;

    // ---- Virtual driver registration ----------------------------------------------
    [[nodiscard]] static bool virtualDriverRegistered();
    void registerVirtualDriver(bool install);

    // ---- UI --------------------------------------------------------------------------
    void drawToolbar();
    void drawMatrix();
    void drawMixer();
    void drawDevices();
    bool drawVoiceControls();
    void drawPttButton();
    bool drawPttSettings();

    // ---- Push-to-talk and MIDI --------------------------------------------------------
    struct MidiLogEntry {
        std::uint32_t message = 0;
        std::uint32_t timeMs = 0;
        midi::MidiBinding::Action action = midi::MidiBinding::Action::None;
        bool pttOpen = false;
    };
    void listMidiDevices();
    void openMidi();
    void applyPttSettings();
    void pollMidi();
    static void onMidi(void* user, std::uint32_t message, std::uint32_t timeMs) noexcept;  // WinMM thread
    void drawStatusBar();
    bool drawStrip(const char* id, const std::string& name, ChannelStrip& strip, const Meter& meter, unsigned color);

    HWND window_;
    Settings settings_;
    std::vector<asio::DriverEntry> drivers_;
    std::vector<wasapi::DeviceInfo> captureDevices_;
    std::vector<wasapi::DeviceInfo> renderDevices_;
    bool driverRegistered_ = false;

    asio::AsioHost host_;
    asio::VirtualAsioServer vasio_;
    std::unique_ptr<AudioEngine> engine_;
    std::shared_ptr<wasapi::WasapiInputEndpoint> mic_;
    std::shared_ptr<wasapi::WasapiOutputEndpoint> stream_;
    bool running_ = false;

    RoutingModel routing_;
    std::unique_ptr<dsp::MixCoefficients> coefficients_ = std::make_unique<dsp::MixCoefficients>();
    std::vector<ChannelGroup> inputGroups_;
    std::vector<ChannelGroup> outputGroups_;
    bool routingDirty_ = false;
    bool settingsDirty_ = false;

    std::vector<Meter> inputMeters_;
    std::vector<Meter> outputMeters_;
    float dspLoad_ = 0.0f;
    float dspPeak_ = 0.0f;

    std::string error_;
    std::vector<std::string> warnings_;

    Log log_;

    // Shared with the engine (audio thread) and the MIDI thread; outlives audio restarts.
    std::shared_ptr<PushToTalk> ptt_ = std::make_shared<PushToTalk>();
    midi::MidiInput midiIn_;
    std::vector<midi::MidiInput::Device> midiDevices_;
    std::string midiError_;
    std::atomic<std::uint32_t> pttBinding_{0};  // MidiBinding::pack(), read on the MIDI thread
    std::atomic<bool> midiLearning_{false};
    SpscRingBuffer<MidiLogEntry> midiEvents_{1024};  // MIDI thread -> GUI (log, monitor, learn)
    std::atomic<std::uint64_t> midiDropped_{0};
    std::deque<std::string> midiMonitor_;  // last messages, newest first
    bool pttWasPassing_ = true;
};

} // namespace afm::app
