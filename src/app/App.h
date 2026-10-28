#pragma once

#include "AsioHost.h"
#include "VirtualAsioServer.h"
#include "afm/engine/AudioEngine.h"
#include "afm/engine/RoutingModel.h"
#include "afm/platform/Win.h"
#include "afm/wasapi/WasapiEndpoints.h"

#include <memory>
#include <string>
#include <vector>

namespace afm::app {

struct Settings {
    std::string driverName;
    double sampleRate = 48000.0;
    bool micEnabled = false;
    std::wstring micId;
    bool micExclusive = true;
    bool streamEnabled = false;
    std::wstring streamId;
    bool streamExclusive = false;
    bool autoStart = true;
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
};

} // namespace afm::app
