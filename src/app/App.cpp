#include "App.h"

#include "imgui.h"

#include <shellapi.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <array>
#include <sstream>

namespace afm::app {

namespace {

constexpr wchar_t kVirtualDriverKey[] = L"SOFTWARE\\ASIO\\AudioFlow Matrix ASIO";

std::wstring exeDirectory() {
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring dir = path;
    return dir.substr(0, dir.find_last_of(L"\\/"));
}

void addGroup(std::vector<ChannelGroup>& groups, std::vector<std::string>& names, std::vector<PanSide>* sides,
              std::string groupName, std::uint32_t count, const std::function<std::string(std::uint32_t)>& nameOf) {
    groups.push_back({std::move(groupName), static_cast<std::uint32_t>(names.size()), count});
    for (std::uint32_t ch = 0; ch < count; ++ch) {
        names.push_back(nameOf(ch));
        if (sides != nullptr) {
            sides->push_back(count < 2 ? PanSide::Mono : (ch % 2 == 0 ? PanSide::Left : PanSide::Right));
        }
    }
}

const ChannelGroup* findGroup(const std::vector<ChannelGroup>& groups, const std::string& name) {
    for (const auto& g : groups) {
        if (g.name == name) return &g;
    }
    return nullptr;
}

} // namespace

App::App(HWND window) : window_(window) {
    // First run never grabs the audio interface on its own: the user picks devices first.
    const bool configured = loadSettings();
    refreshDevices();

    std::string error;
    if (!vasio_.open(error)) {
        warnings_.push_back(error);
    }

    const bool knownDriver = std::any_of(drivers_.begin(), drivers_.end(),
                                         [&](const auto& d) { return d.name == settings_.driverName; });
    if (configured && settings_.autoStart && knownDriver) {
        startAudio();
    }
}

App::~App() {
    saveSettings();
    stopAudio();
    vasio_.close();
}

// ---- Audio lifecycle ----------------------------------------------------------------------

void App::refreshDevices() {
    drivers_ = asio::listDrivers();
    captureDevices_ = wasapi::listDevices(true);
    renderDevices_ = wasapi::listDevices(false);
    driverRegistered_ = virtualDriverRegistered();
    if (settings_.driverName.empty() && !drivers_.empty()) {
        settings_.driverName = drivers_.front().name;
    }
}

bool App::startAudio() {
    stopAudio();
    error_.clear();
    warnings_.clear();

    auto driver = std::find_if(drivers_.begin(), drivers_.end(),
                               [&](const auto& d) { return d.name == settings_.driverName; });
    if (driver == drivers_.end()) {
        error_ = "Select a master ASIO driver in the Devices tab.";
        return false;
    }
    std::string error;
    if (!host_.load(*driver, window_, settings_.sampleRate, error)) {
        error_ = error;
        return false;
    }

    const double rate = host_.sampleRate();
    const std::uint32_t block = host_.blockFrames();
    engine_ = std::make_unique<AudioEngine>();

    if (vasio_.isOpen()) {
        engine_->addInput(vasio_.inputEndpoint());
        engine_->addOutput(vasio_.outputEndpoint());
    }
    if (settings_.micEnabled && !settings_.micId.empty()) {
        wasapi::StreamConfig cfg{settings_.micId, "Mic", settings_.micExclusive, rate, block};
        if ((mic_ = wasapi::WasapiInputEndpoint::open(cfg, error))) {
            engine_->addInput(mic_);
        } else {
            warnings_.push_back("Microphone: " + error);
        }
    }
    if (settings_.streamEnabled && !settings_.streamId.empty()) {
        wasapi::StreamConfig cfg{settings_.streamId, "Stream", settings_.streamExclusive, rate, block};
        if ((stream_ = wasapi::WasapiOutputEndpoint::open(cfg, error))) {
            engine_->addOutput(stream_);
        } else {
            warnings_.push_back("Stream output: " + error);
        }
    }

    AudioEngine::Config config;
    config.format = {rate, block};
    config.hardwareInputs = static_cast<std::uint32_t>(host_.inputs().size());
    config.hardwareOutputs = static_cast<std::uint32_t>(host_.outputs().size());
    if (!engine_->prepare(config)) {
        error_ = "Too many channels for the routing matrix (max " + std::to_string(kMaxInputs) + ").";
        stopAudio();
        return false;
    }

    rebuildLayout();

    if (!host_.start(*engine_, error)) {
        error_ = error;
        stopAudio();
        return false;
    }
    vasio_.begin(rate, block, static_cast<std::uint32_t>(host_.inputLatency()),
                 static_cast<std::uint32_t>(host_.outputLatency()));
    running_ = true;
    return true;
}

void App::stopAudio() {
    vasio_.end();
    host_.stop();  // no more callbacks after this returns
    mic_.reset();
    stream_.reset();
    engine_.reset();
    host_.unload();  // frees the interface for other applications
    running_ = false;
    dspLoad_ = dspPeak_ = 0.0f;
}

void App::restartAudio() {
    saveSettings();
    startAudio();
}

void App::rebuildLayout() {
    std::vector<std::string> inNames, outNames;
    std::vector<PanSide> outSides;
    inputGroups_.clear();
    outputGroups_.clear();

    const auto& hwIn = host_.inputs();
    const auto& hwOut = host_.outputs();
    addGroup(inputGroups_, inNames, nullptr, host_.driverName(), static_cast<std::uint32_t>(hwIn.size()),
             [&](std::uint32_t ch) { return hwIn[ch].name.empty() ? "In " + std::to_string(ch + 1) : hwIn[ch].name; });
    addGroup(outputGroups_, outNames, &outSides, host_.driverName(), static_cast<std::uint32_t>(hwOut.size()),
             [&](std::uint32_t ch) { return hwOut[ch].name.empty() ? "Out " + std::to_string(ch + 1) : hwOut[ch].name; });
    for (const auto& ep : engine_->inputEndpoints()) {
        addGroup(inputGroups_, inNames, nullptr, std::string(ep->name()), ep->numChannels(),
                 [&](std::uint32_t ch) { return ep->channelName(ch); });
    }
    for (const auto& ep : engine_->outputEndpoints()) {
        addGroup(outputGroups_, outNames, &outSides, std::string(ep->name()), ep->numChannels(),
                 [&](std::uint32_t ch) { return ep->channelName(ch); });
    }

    routing_.setLayout(std::move(inNames), std::move(outNames), std::move(outSides));
    if (!routing_.hasRoutes()) {
        applyDefaultRouting();
    }
    inputMeters_.assign(routing_.numInputs(), Meter{});
    outputMeters_.assign(routing_.numOutputs(), Meter{});
    publishMix();
}

// Sensible first-run routing for a DJ/streaming setup.
void App::applyDefaultRouting() {
    const ChannelGroup* hwIn = inputGroups_.empty() ? nullptr : &inputGroups_.front();
    const ChannelGroup* hwOut = outputGroups_.empty() ? nullptr : &outputGroups_.front();
    const ChannelGroup* clientOut = findGroup(inputGroups_, "AudioFlow ASIO");
    const ChannelGroup* clientIn = findGroup(outputGroups_, "AudioFlow ASIO");
    const ChannelGroup* mic = findGroup(inputGroups_, "Mic");
    const ChannelGroup* stream = findGroup(outputGroups_, "Stream");

    auto route = [&](const ChannelGroup* in, std::uint32_t i, const ChannelGroup* out, std::uint32_t o) {
        if (in != nullptr && out != nullptr && i < in->count && o < out->count) {
            routing_.at(out->begin + o, in->begin + i).routed = true;
        }
    };
    // Traktor decks -> interface outputs, interface inputs (DVS timecode) -> Traktor.
    for (std::uint32_t k = 0; k < 8; ++k) route(clientOut, k, hwOut, k);
    for (std::uint32_t k = 0; k < 8; ++k) route(hwIn, k, clientIn, k);
    // Master + mic -> stream.
    route(clientOut, 0, stream, 0);
    route(clientOut, 1, stream, 1);
    if (mic != nullptr && mic->count == 1) {
        route(mic, 0, stream, 0);
        route(mic, 0, stream, 1);
    } else {
        route(mic, 0, stream, 0);
        route(mic, 1, stream, 1);
    }
}

void App::publishMix() {
    routing_.compile(*coefficients_);
    if (engine_) {
        engine_->setMix(*coefficients_);
    }
}

void App::pollTelemetry() {
    std::array<float, kMaxInputs> inPeak{};
    std::array<float, kMaxOutputs> outPeak{};
    float load = 0.0f;
    bool any = false;
    TelemetryFrame t;
    while (engine_ && engine_->popTelemetry(t)) {
        any = true;
        for (std::uint32_t ch = 0; ch < t.numInputs && ch < kMaxInputs; ++ch) inPeak[ch] = std::max(inPeak[ch], t.inputPeak[ch]);
        for (std::uint32_t ch = 0; ch < t.numOutputs && ch < kMaxOutputs; ++ch) outPeak[ch] = std::max(outPeak[ch], t.outputPeak[ch]);
        load = std::max(load, t.dspLoad);
    }

    const float dt = ImGui::GetIO().DeltaTime;
    const double now = ImGui::GetTime();
    auto update = [&](std::vector<Meter>& meters, const float* peaks) {
        for (std::size_t ch = 0; ch < meters.size(); ++ch) {
            Meter& m = meters[ch];
            const float db = peaks[ch] > 1e-5f ? 20.0f * std::log10(peaks[ch]) : -90.0f;
            m.levelDb = std::max(db, m.levelDb - 30.0f * dt);  // 30 dB/s release
            if (db >= m.holdDb) {
                m.holdDb = db;
                m.holdTime = now;
            } else if (now - m.holdTime > 1.5) {
                m.holdDb = std::max(m.levelDb, m.holdDb - 20.0f * dt);
            }
        }
    };
    update(inputMeters_, inPeak.data());
    update(outputMeters_, outPeak.data());

    if (any) {
        dspLoad_ += 0.1f * (load - dspLoad_);
        dspPeak_ = std::max(load, dspPeak_ - 0.2f * dt);
    }
}

// ---- Frame --------------------------------------------------------------------------------

void App::frame() {
    if (running_ && host_.takeResetRequest()) {
        restartAudio();  // the driver changed buffer size / rate
    }
    if (routingDirty_) {
        publishMix();
        routingDirty_ = false;
        settingsDirty_ = true;
    }
    if (settingsDirty_ && !ImGui::IsAnyItemActive()) {
        saveSettings();
        settingsDirty_ = false;
    }
    pollTelemetry();

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;
    if (ImGui::Begin("AudioFlow Matrix", nullptr, flags)) {
        drawToolbar();
        const float statusHeight = ImGui::GetFrameHeightWithSpacing();
        if (ImGui::BeginChild("content", ImVec2(0, -statusHeight))) {
            if (ImGui::BeginTabBar("tabs")) {
                if (ImGui::BeginTabItem("Matrix")) {
                    drawMatrix();
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Mixer")) {
                    drawMixer();
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Devices")) {
                    drawDevices();
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
        }
        ImGui::EndChild();
        drawStatusBar();
    }
    ImGui::End();
}

// ---- Persistence --------------------------------------------------------------------------

std::wstring App::settingsPath() const {
    wchar_t appData[MAX_PATH] = {};
    std::wstring dir;
    if (GetEnvironmentVariableW(L"APPDATA", appData, MAX_PATH) > 0) {
        dir = std::wstring(appData) + L"\\AudioFlowMatrix";
        CreateDirectoryW(dir.c_str(), nullptr);
    } else {
        dir = exeDirectory();
    }
    return dir + L"\\settings.ini";
}

bool App::loadSettings() {
    std::ifstream in{std::filesystem::path(settingsPath())};
    if (!in) return false;
    std::stringstream routingLines;
    std::string line;
    while (std::getline(in, line)) {
        if (line.find('\t') != std::string::npos) {
            routingLines << line << '\n';
            continue;
        }
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq);
        const std::string value = line.substr(eq + 1);
        if (key == "driver") settings_.driverName = value;
        else if (key == "rate") settings_.sampleRate = std::strtod(value.c_str(), nullptr);
        else if (key == "mic.enabled") settings_.micEnabled = value == "1";
        else if (key == "mic.id") settings_.micId = win::toWide(value);
        else if (key == "mic.exclusive") settings_.micExclusive = value == "1";
        else if (key == "stream.enabled") settings_.streamEnabled = value == "1";
        else if (key == "stream.id") settings_.streamId = win::toWide(value);
        else if (key == "stream.exclusive") settings_.streamExclusive = value == "1";
        else if (key == "autostart") settings_.autoStart = value == "1";
    }
    routing_.load(routingLines);
    return true;
}

void App::saveSettings() const {
    std::ofstream out(std::filesystem::path(settingsPath()), std::ios::trunc);
    if (!out) return;
    out << "driver=" << settings_.driverName << '\n'
        << "rate=" << settings_.sampleRate << '\n'
        << "mic.enabled=" << settings_.micEnabled << '\n'
        << "mic.id=" << win::toUtf8(settings_.micId) << '\n'
        << "mic.exclusive=" << settings_.micExclusive << '\n'
        << "stream.enabled=" << settings_.streamEnabled << '\n'
        << "stream.id=" << win::toUtf8(settings_.streamId) << '\n'
        << "stream.exclusive=" << settings_.streamExclusive << '\n'
        << "autostart=" << settings_.autoStart << '\n';
    routing_.save(out);
}

// ---- Virtual driver registration ------------------------------------------------------------

bool App::virtualDriverRegistered() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, kVirtualDriverKey, 0, KEY_READ | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS) {
        return false;
    }
    RegCloseKey(key);
    return true;
}

void App::registerVirtualDriver(bool install) {
    const std::wstring dll = exeDirectory() + L"\\AudioFlowMatrixASIO.dll";
    if (GetFileAttributesW(dll.c_str()) == INVALID_FILE_ATTRIBUTES) {
        error_ = "AudioFlowMatrixASIO.dll was not found next to the executable.";
        return;
    }
    const std::wstring params = std::wstring(install ? L"/s \"" : L"/u /s \"") + dll + L"\"";
    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.hwnd = window_;
    sei.lpVerb = L"runas";  // UAC prompt: writing HKLM needs administrator rights
    sei.lpFile = L"regsvr32.exe";
    sei.lpParameters = params.c_str();
    sei.nShow = SW_HIDE;
    if (!ShellExecuteExW(&sei)) {
        error_ = "Driver registration was cancelled.";
        return;
    }
    if (sei.hProcess != nullptr) {
        WaitForSingleObject(sei.hProcess, 30000);
        CloseHandle(sei.hProcess);
    }
    driverRegistered_ = virtualDriverRegistered();
    if (install && !driverRegistered_) {
        error_ = "Driver registration failed.";
    }
}

} // namespace afm::app
