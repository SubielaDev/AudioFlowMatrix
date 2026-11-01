#pragma once

#include "AsioHost.h"
#include "Driver.h"
#include "Keyboard.h"
#include "Log.h"
#include "UpdateCheck.h"
#include "VirtualAsioServer.h"
#include "afm/core/SpscRingBuffer.h"
#include "afm/engine/AudioEngine.h"
#include "afm/engine/PushToTalk.h"
#include "afm/engine/RoutingModel.h"
#include "afm/platform/Win.h"
#include "afm/midi/KeyMap.h"
#include "afm/midi/MidiInput.h"
#include "afm/midi/MidiMap.h"
#include "afm/midi/MidiMessage.h"
#include "afm/wasapi/WasapiEndpoints.h"

#include <atomic>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace afm::app {

struct Settings {
    std::string driverName;
    double sampleRate = 0.0;  // 0 = keep the rate the driver is already running at
    bool micEnabled = false;
    std::wstring micId;
    bool micExclusive = false;  // shared: games, Discord etc. can use the mic too
    bool micDenoise = false;
    bool micGate = false;
    float micGateThresholdDb = -45.0f;
    bool streamEnabled = false;
    std::wstring streamId;
    bool streamExclusive = false;
    bool loopbackEnabled = false;  // capture what Windows plays on a render device
    std::wstring loopbackId;
    bool autoStart = true;
    bool minimizeToTray = false;  // minimized: hide from the taskbar, icon in the notification area
    int uiFps = 60;                  // GUI frames per second: 15, 30, 60, or 0 = the monitor's refresh rate
    bool uiSlowInBackground = true;  // 15 fps while another window has the focus
    bool pttEnabled = false;   // push-to-talk on mic -> stream (optional, off by default)
    bool pttToggle = false;    // false: hold to talk, true: each press toggles
    std::string midiDevice;    // MIDI input port name; empty = none
    // Display names chosen in the Matrix/Mixer, keyed by the channel's own name.
    std::map<std::string, std::string> inputAliases;
    std::map<std::string, std::string> outputAliases;
    midi::MidiMap midiMap;     // MIDI controls -> PTT / channel mutes
    midi::KeyMap keyMap;       // computer-keyboard keys -> the same targets
    bool updateCheck = true;        // ask GitHub once a day whether a newer release exists
    std::string updateLatest;       // latest release tag seen, e.g. "v0.3.2"
    std::string updateIgnored;      // tag the user chose to ignore
    long long updateLastCheck = 0;  // unix time of the last successful check
};

// Group of consecutive bus channels that belong to one device, for display.
struct ChannelGroup {
    std::string name;
    std::uint32_t begin = 0;
    std::uint32_t count = 0;
};

// Performance test (--perf-test, see main.cpp): a fixed fake layout on the Mixer tab with
// synthetic meter levels, no audio device, MIDI or network, and nothing saved.
struct PerfScene {
    bool enabled = false;
    bool active = true;  // meters move like music; false: silence, nothing changes on screen
    const char* tab = "mixer";  // tab shown first: "mixer", "matrix" or "devices"
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
    explicit App(HWND window, PerfScene perf = {});
    ~App();

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    // One GUI frame: housekeeping + ImGui drawing.
    void frame();
    // While the window is minimised (no frames): keeps MIDI mappings working.
    void idle();
    [[nodiscard]] bool minimizeToTray() const noexcept { return settings_.minimizeToTray; }
    // GUI frames per second right now, from Settings > Window and whether the window has the focus.
    [[nodiscard]] double uiFrameRate() const;

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
    void pollVirtualClient();
    void setupPerfScene();
    void perfPeaks(float* inPeak, float* outPeak, float& load) const;

    // ---- Persistence ---------------------------------------------------------------
    [[nodiscard]] std::wstring settingsPath() const;
    bool loadSettings();  // false on first run (no settings file yet)
    void saveSettings() const;

    // ---- Virtual driver registration ----------------------------------------------
    void registerVirtualDriver(bool install);

    // ---- UI --------------------------------------------------------------------------
    void drawToolbar();
    void drawUpdateBanner();
    void startUpdateCheck();
    void pollUpdateCheck();
    bool drawUpdateCheckbox(const char* label);
    void drawMatrix();
    void drawMixer();
    void drawDevices();
    void drawSettings();
    bool drawVoiceControls();
    void drawPttButton();
    bool drawPttSettings();
    void drawMidi();

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
    void applyMidiAction(const std::string& target, const midi::MidiEvent& event);
    void startMidiLearn(const std::string& target);
    void cancelMidiLearn();
    void clearMidiMapping(const std::string& target);
    void syncPttBinding();
    static void onMidi(void* user, std::uint32_t message, std::uint32_t timeMs) noexcept;  // WinMM thread
    void toggleMute(const std::string& target, const std::string& source);

    struct KeyLogEntry {
        midi::KeyEvent event;
    };
    void startKeyboard();
    void pollKeys();
    void clearKeyMapping(const std::string& target);
    static void onKey(void* user, const midi::KeyEvent& event) noexcept;  // keyboard thread
    void drawStatusBar();
    bool drawStrip(const char* id, bool input, std::uint32_t ch, unsigned color, float width);
    [[nodiscard]] const std::string& inputLabel(std::uint32_t i) const;
    [[nodiscard]] const std::string& outputLabel(std::uint32_t o) const;
    void drawRenameField(bool input, const std::string& name);

    HWND window_;
    Settings settings_;
    std::vector<asio::DriverEntry> drivers_;
    std::vector<wasapi::DeviceInfo> captureDevices_;
    std::vector<wasapi::DeviceInfo> renderDevices_;
    driver::Status driverStatus_ = driver::Status::NotRegistered;

    asio::AsioHost host_;
    asio::VirtualAsioServer vasio_;
    std::unique_ptr<AudioEngine> engine_;
    std::shared_ptr<wasapi::WasapiInputEndpoint> mic_;
    std::shared_ptr<wasapi::WasapiOutputEndpoint> stream_;
    std::shared_ptr<wasapi::WasapiInputEndpoint> loopback_;
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
    std::atomic<bool> midiLearning_{false};  // MIDI and keyboard threads: don't act while learning
    std::string midiLearnTarget_;             // GUI thread: what the next control or key gets mapped to
    SpscRingBuffer<MidiLogEntry> midiEvents_{1024};  // MIDI thread -> GUI (log, monitor, learn)
    std::atomic<std::uint64_t> midiDropped_{0};
    std::deque<std::string> midiMonitor_;  // last messages, newest first
    bool pttWasPassing_ = true;

    // Key shortcuts. Unmapped keys are never logged or kept: the ring only feeds Learn and
    // the mappings, and is emptied every frame.
    KeyboardInput keyboard_;
    std::string keyboardError_;
    std::atomic<std::uint32_t> pttKey_{0};       // KeyBinding::pack(), read on the keyboard thread
    std::atomic<bool> keysSuspended_{false};     // typing in one of our text fields: presses ignored
    SpscRingBuffer<KeyLogEntry> keyEvents_{256};  // keyboard thread -> GUI
    midi::KeyLearner keyLearner_;
    unsigned long long navBlockedUntil_ = 0;  // GetTickCount64(): ImGui keyboard nav off around Learn

    // Virtual ASIO client: late-block counts sampled once a second (oldest first, 10 s), and
    // what was last written to the log.
    std::deque<std::uint64_t> clientLateHistory_ = std::deque<std::uint64_t>(10, 0);
    double clientSampleTime_ = 0.0;
    std::uint64_t clientLateLogged_ = 0;
    bool clientStallLogged_ = false;

    int hoveredRow_ = -1;     // matrix crosshair, from the previous frame
    int hoveredColumn_ = -1;

    PerfScene perf_;
    unsigned displayHz_ = 0;     // monitor refresh rate, refreshed once a second
    double displayHzTime_ = -1.0;
    bool perfTabPending_ = true;  // select the Mixer tab once

    UpdateCheck updateCheck_;
    bool updatePolled_ = true;           // the last check's result has been handled
    bool updateBannerHidden_ = false;    // "Later": hidden until the next start
    std::string updateError_;            // why the last check failed, shown in Settings
};

} // namespace afm::app
