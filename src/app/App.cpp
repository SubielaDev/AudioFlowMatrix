#include "App.h"
#include "I18n.h"
#include "Version.h"
#include "WindowChrome.h"

#include "imgui.h"

#include <shellapi.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <array>
#include <sstream>

namespace afm::app {

namespace {

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

// Settings lines for channel names: "alias.in=<name>|<display name>". Backslash escapes keep
// '|', tabs (routing lines contain tabs) and line breaks out of the stored text.
std::string escapeSetting(const std::string& text) {
    std::string out;
    for (char c : text) {
        if (c == '\\' || c == '|') out += '\\', out += c;
        else if (c == '\t') out += "\\t";
        else if (c == '\n' || c == '\r') out += "\\n";
        else out += c;
    }
    return out;
}

// Splits "<name>|<display name>" (both escaped); false if there is no unescaped '|'.
bool parseAlias(const std::string& value, std::string& name, std::string& alias) {
    std::string* target = &name;
    name.clear();
    alias.clear();
    bool split = false;
    for (std::size_t i = 0; i < value.size(); ++i) {
        const char c = value[i];
        if (c == '\\' && i + 1 < value.size()) {
            const char next = value[++i];
            *target += next == 't' ? '\t' : next == 'n' ? ' ' : next;
        } else if (c == '|' && !split) {
            target = &alias;
            split = true;
        } else {
            *target += c;
        }
    }
    return split && !name.empty() && !alias.empty();
}

// printf into a std::string, for messages built from translated formats.
std::string formatted(const char* format, ...) {
    char text[512];
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    return text;
}

const ChannelGroup* findGroup(const std::vector<ChannelGroup>& groups, const std::string& name) {
    for (const auto& g : groups) {
        if (g.name == name) return &g;
    }
    return nullptr;
}

} // namespace

App::App(HWND window, PerfScene perf) : window_(window), perf_(perf) {
    log_.open();
    log_.write("AudioFlow Matrix %s started", AFM_VERSION_STRING);

    // The Windows display language until the user picks one in Settings.
    i18n::setLanguage(i18n::systemLanguage());
    // First run never grabs the audio interface on its own: the user picks devices first.
    const bool configured = loadSettings();
    if (perf_.enabled) {
        log_.write("Performance test: %s meters, nothing is saved", perf_.active ? "moving" : "silent");
        setupPerfScene();
        return;
    }
    refreshDevices();
    applyPttSettings();
    openMidi();

    // At most once a day; a failed check (offline) is retried at the next start.
    const long long now = static_cast<long long>(std::time(nullptr));
    if (settings_.updateCheck && now - settings_.updateLastCheck >= 24 * 60 * 60) startUpdateCheck();

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
    midiIn_.close();  // before the members its callback touches go away
    saveSettings();
    stopAudio();
    vasio_.close();
}

// ---- Audio lifecycle ----------------------------------------------------------------------

void App::refreshDevices() {
    drivers_ = asio::listDrivers();
    captureDevices_ = wasapi::listDevices(true);
    renderDevices_ = wasapi::listDevices(false);
    driverStatus_ = driver::status();
    log_.write("Virtual ASIO driver: registered from \"%s\" (%s); this build installs to \"%s\"",
               win::toUtf8(driver::registeredPath()).c_str(),
               driverStatus_ == driver::Status::Registered        ? "this build"
               : driverStatus_ == driver::Status::NotRegistered ? "not registered"
                                                                 : "another build",
               win::toUtf8(driver::installPath()).c_str());
    listMidiDevices();
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
        error_ = tr("Select a master ASIO driver in the Devices tab.");
        return false;
    }
    std::string error;
    if (!host_.load(*driver, window_, settings_.sampleRate, error)) {
        error_ = error;
        return false;
    }
    if (host_.bufferSizeFallback()) {
        warnings_.push_back(formatted(tr("The ASIO driver did not report its buffer size; using %u samples."),
                                      static_cast<unsigned>(asio::AsioHost::kFallbackBlockFrames)));
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
        cfg.denoise = settings_.micDenoise;
        cfg.gate = settings_.micGate;
        cfg.gateThresholdDb = settings_.micGateThresholdDb;
        if ((mic_ = wasapi::WasapiInputEndpoint::open(cfg, error))) {
            engine_->addInput(mic_);
        } else {
            warnings_.push_back(formatted(tr("Microphone: %s"), error.c_str()));
        }
    }
    if (settings_.loopbackEnabled && !settings_.loopbackId.empty()) {
        wasapi::StreamConfig cfg{settings_.loopbackId, "Loopback", false, rate, block};
        cfg.loopback = true;
        if ((loopback_ = wasapi::WasapiInputEndpoint::open(cfg, error))) {
            engine_->addInput(loopback_);
        } else {
            warnings_.push_back(formatted(tr("Loopback: %s"), error.c_str()));
        }
    }
    if (settings_.streamEnabled && !settings_.streamId.empty()) {
        wasapi::StreamConfig cfg{settings_.streamId, "Stream", settings_.streamExclusive, rate, block};
        if ((stream_ = wasapi::WasapiOutputEndpoint::open(cfg, error))) {
            engine_->addOutput(stream_);
        } else {
            warnings_.push_back(formatted(tr("Stream output: %s"), error.c_str()));
        }
    }

    AudioEngine::Config config;
    config.format = {rate, block};
    config.hardwareInputs = static_cast<std::uint32_t>(host_.inputs().size());
    config.hardwareOutputs = static_cast<std::uint32_t>(host_.outputs().size());
    if (!engine_->prepare(config)) {
        error_ = formatted(tr("Too many channels for the routing matrix (max %u)."), static_cast<unsigned>(kMaxInputs));
        stopAudio();
        return false;
    }

    rebuildLayout();

    // Push-to-talk acts on every mic channel -> every stream channel crosspoint.
    std::bitset<kMaxInputs> pttIn;
    std::bitset<kMaxOutputs> pttOut;
    if (const ChannelGroup* g = findGroup(inputGroups_, "Mic")) {
        for (std::uint32_t k = 0; k < g->count; ++k) pttIn.set(g->begin + k);
    }
    if (const ChannelGroup* g = findGroup(outputGroups_, "Stream")) {
        for (std::uint32_t k = 0; k < g->count; ++k) pttOut.set(g->begin + k);
    }
    engine_->setPtt(pttIn, pttOut, ptt_);

    if (!host_.start(*engine_, error)) {
        error_ = error;
        stopAudio();
        return false;
    }
    vasio_.begin(rate, block, static_cast<std::uint32_t>(host_.inputLatency()),
                 static_cast<std::uint32_t>(host_.outputLatency()));
    running_ = true;
    log_.write("Audio started: \"%s\", %.0f Hz, %u samples%s | mic: %s | loopback: %s | stream: %s | "
               "PTT routes: %zu mic ch -> %zu stream ch",
               host_.driverName().c_str(), rate, block, host_.bufferSizeFallback() ? " (fallback)" : "",
               mic_ ? mic_->info().format.c_str() : "off", loopback_ ? loopback_->info().format.c_str() : "off",
               stream_ ? stream_->info().format.c_str() : "off", pttIn.count(), pttOut.count());
    return true;
}

void App::stopAudio() {
    if (running_) log_.write("Audio stopped");
    vasio_.end();
    host_.stop();  // no more callbacks after this returns
    mic_.reset();
    loopback_.reset();
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
    if (perf_.enabled) {
        perfPeaks(inPeak.data(), outPeak.data(), load);
        any = true;
    }
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

// ---- Performance test ---------------------------------------------------------------------

// A layout like a typical setup: an 8x8 interface, Traktor's 8x8, mic, loopback and stream.
void App::setupPerfScene() {
    std::vector<std::string> inNames, outNames;
    std::vector<PanSide> outSides;
    inputGroups_.clear();
    outputGroups_.clear();
    const auto numbered = [](std::string prefix) {
        return [prefix](std::uint32_t ch) { return prefix + " " + std::to_string(ch + 1); };
    };
    const auto stereo = [](std::string prefix) {
        return [prefix](std::uint32_t ch) { return prefix + (ch == 0 ? " L" : " R"); };
    };
    addGroup(inputGroups_, inNames, nullptr, "Interface", 8, numbered("Input"));
    addGroup(outputGroups_, outNames, &outSides, "Interface", 8, numbered("Output"));
    addGroup(inputGroups_, inNames, nullptr, "AudioFlow ASIO", 8, numbered("ASIO Client Out"));
    addGroup(outputGroups_, outNames, &outSides, "AudioFlow ASIO", 8, numbered("ASIO Client In"));
    addGroup(inputGroups_, inNames, nullptr, "Mic", 1, [](std::uint32_t) { return std::string("Mic"); });
    addGroup(inputGroups_, inNames, nullptr, "Loopback", 2, stereo("Loopback"));
    addGroup(outputGroups_, outNames, &outSides, "Stream", 2, stereo("Stream"));
    routing_.setLayout(std::move(inNames), std::move(outNames), std::move(outSides));
    routing_.clearRoutes();
    applyDefaultRouting();
    inputMeters_.assign(routing_.numInputs(), Meter{});
    outputMeters_.assign(routing_.numOutputs(), Meter{});
    publishMix();
}

// Synthetic peaks: a beat every half second and slower swells, different on every channel,
// so the meters move like they do with music. Silent: all zero, the screen settles.
void App::perfPeaks(float* inPeak, float* outPeak, float& load) const {
    load = 0.05f;
    if (!perf_.active) return;
    const double t = ImGui::GetTime();
    const auto level = [t](std::uint32_t ch) {
        const double beat = std::exp(-6.0 * std::fmod(t + ch * 0.07, 0.5));
        const double swell = 0.5 + 0.5 * std::sin(t * (0.7 + 0.13 * ch) + ch);
        return static_cast<float>(0.05 + 0.6 * beat * swell);
    };
    for (std::uint32_t ch = 0; ch < routing_.numInputs(); ++ch) inPeak[ch] = level(ch);
    for (std::uint32_t ch = 0; ch < routing_.numOutputs(); ++ch) outPeak[ch] = level(ch + 50);
    load = static_cast<float>(0.12 + 0.03 * std::sin(t));
}

// ---- Frame --------------------------------------------------------------------------------

double App::uiFrameRate() const {
    double rate = settings_.uiFps > 0 ? settings_.uiFps : (displayHz_ > 0 ? displayHz_ : 60.0);
    if (settings_.uiSlowInBackground && GetForegroundWindow() != window_) rate = std::min(rate, 15.0);
    return rate;
}

// Once a second: keeps the 10-second window of late client blocks and logs changes, also
// while minimized (idle), so a session leaves a record of when the clicks happened.
void App::pollVirtualClient() {
    const double now = static_cast<double>(GetTickCount64()) / 1000.0;
    if (now - clientSampleTime_ < 1.0) return;
    clientSampleTime_ = now;
    const std::uint64_t late = vasio_.lateBlocks();
    clientLateHistory_.pop_front();
    clientLateHistory_.push_back(late);
    if (late != clientLateLogged_) {
        const double ms = running_ ? vasio_.safetyBlocks() * host_.blockFrames() * 1000.0 / host_.sampleRate() : 0.0;
        log_.write("Virtual ASIO: %llu late client block(s) (%llu in total); safety buffer now %u blocks (%.1f ms)",
                   static_cast<unsigned long long>(late - clientLateLogged_), static_cast<unsigned long long>(late),
                   vasio_.safetyBlocks(), ms);
        clientLateLogged_ = late;
    }
    const bool stalled = vasio_.clientConnected() && vasio_.clientStalled();
    if (stalled != clientStallLogged_) {
        log_.write("Virtual ASIO: client (PID %u) %s", vasio_.clientPid(),
                   stalled ? "stopped sending audio while still connected" : "is sending audio again");
        clientStallLogged_ = stalled;
    }
}

void App::idle() {
    pollVirtualClient();
    pollMidi();
    if (routingDirty_) {
        publishMix();
        routingDirty_ = false;
        saveSettings();
    }
}

void App::frame() {
    if (running_ && host_.takeResetRequest()) {
        restartAudio();  // the driver changed buffer size / rate
    }
    pollMidi();  // before publishing, so MIDI mutes reach the audio this frame
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
    pollVirtualClient();
    pollUpdateCheck();
    if (ImGui::GetTime() - displayHzTime_ >= 1.0) {  // the window may have moved to another monitor
        displayHz_ = chrome::refreshRate(window_);
        displayHzTime_ = ImGui::GetTime();
    }

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float titleHeight = chrome::drawTitleBar(window_);
    ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x, vp->WorkPos.y + titleHeight));
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, vp->WorkSize.y - titleHeight));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;
    if (ImGui::Begin("AudioFlow Matrix", nullptr, flags)) {
        drawToolbar();
        drawUpdateBanner();
        const float statusHeight = ImGui::GetFrameHeightWithSpacing();
        if (ImGui::BeginChild("content", ImVec2(0, -statusHeight))) {
            if (ImGui::BeginTabBar("tabs")) {
                const bool selectTab = perf_.enabled && perfTabPending_;
                const auto perfTab = [&](const char* name) {
                    return selectTab && std::strcmp(perf_.tab, name) == 0 ? ImGuiTabItemFlags_SetSelected : 0;
                };
                if (ImGui::BeginTabItem(tr("Matrix###tab-matrix"), nullptr, perfTab("matrix"))) {
                    drawMatrix();
                    ImGui::EndTabItem();
                }
                const ImGuiTabItemFlags mixerFlags = perfTab("mixer");
                perfTabPending_ = false;
                if (ImGui::BeginTabItem(tr("Mixer###tab-mixer"), nullptr, mixerFlags)) {
                    drawMixer();
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem(tr("Devices###tab-devices"), nullptr, perfTab("devices"))) {
                    drawDevices();
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("MIDI###tab-midi")) {
                    drawMidi();
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem(tr("Settings###tab-settings"))) {
                    drawSettings();
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
        // Older versions saved "rate", which always defaulted to 48000 Hz and so was never a
        // deliberate choice; it is ignored and the driver's own rate is used instead.
        else if (key == "samplerate") settings_.sampleRate = std::strtod(value.c_str(), nullptr);
        else if (key == "mic.enabled") settings_.micEnabled = value == "1";
        else if (key == "mic.id") settings_.micId = win::toWide(value);
        // "mic.exclusive" was always saved, defaulting to on, so it was rarely a choice; it is
        // ignored and the mic starts shared (exclusive mode locks games and other apps out).
        else if (key == "mic.exclusive.v2") settings_.micExclusive = value == "1";
        else if (key == "mic.denoise") settings_.micDenoise = value == "1";
        else if (key == "mic.gate") settings_.micGate = value == "1";
        else if (key == "mic.gate.threshold") settings_.micGateThresholdDb = std::strtof(value.c_str(), nullptr);
        else if (key == "stream.enabled") settings_.streamEnabled = value == "1";
        else if (key == "stream.id") settings_.streamId = win::toWide(value);
        else if (key == "stream.exclusive") settings_.streamExclusive = value == "1";
        else if (key == "loopback.enabled") settings_.loopbackEnabled = value == "1";
        else if (key == "loopback.id") settings_.loopbackId = win::toWide(value);
        else if (key == "autostart") settings_.autoStart = value == "1";
        else if (key == "tray.minimize") settings_.minimizeToTray = value == "1";
        else if (key == "ui.fps") {
            const int fps = std::atoi(value.c_str());
            settings_.uiFps = fps == 0 || fps == 15 || fps == 30 || fps == 60 ? fps : 60;
        }
        else if (key == "ui.slow_background") settings_.uiSlowInBackground = value == "1";
        else if (key == "ptt.enabled") settings_.pttEnabled = value == "1";
        else if (key == "ptt.toggle") settings_.pttToggle = value == "1";
        else if (key == "midimap") settings_.midiMap.load(value);
        // Before the MIDI tab, the PTT mapping had a key of its own.
        else if (key == "ptt.midi") settings_.midiMap.assign(midi::MidiMap::pttTarget(), midi::MidiBinding::parse(value));
        else if (key == "midi.device") settings_.midiDevice = value;
        else if (key == "alias.in" || key == "alias.out") {
            std::string name, alias;
            if (parseAlias(value, name, alias)) (key == "alias.in" ? settings_.inputAliases : settings_.outputAliases)[name] = alias;
        }
        else if (key == "language") i18n::setLanguage(i18n::fromCode(value, i18n::language()));
        else if (key == "update.check") settings_.updateCheck = value == "1";
        else if (key == "update.latest") settings_.updateLatest = version::parse(value) ? value : std::string{};
        else if (key == "update.ignored") settings_.updateIgnored = value;
        else if (key == "update.last") settings_.updateLastCheck = std::strtoll(value.c_str(), nullptr, 10);
    }
    routing_.load(routingLines);
    return true;
}

void App::saveSettings() const {
    if (perf_.enabled) return;  // the fake test layout must never replace the user's
    std::ofstream out(std::filesystem::path(settingsPath()), std::ios::trunc);
    if (!out) return;
    out << "driver=" << settings_.driverName << '\n'
        << "samplerate=" << settings_.sampleRate << '\n'
        << "mic.enabled=" << settings_.micEnabled << '\n'
        << "mic.id=" << win::toUtf8(settings_.micId) << '\n'
        << "mic.exclusive.v2=" << settings_.micExclusive << '\n'
        << "mic.denoise=" << settings_.micDenoise << '\n'
        << "mic.gate=" << settings_.micGate << '\n'
        << "mic.gate.threshold=" << settings_.micGateThresholdDb << '\n'
        << "stream.enabled=" << settings_.streamEnabled << '\n'
        << "stream.id=" << win::toUtf8(settings_.streamId) << '\n'
        << "stream.exclusive=" << settings_.streamExclusive << '\n'
        << "loopback.enabled=" << settings_.loopbackEnabled << '\n'
        << "loopback.id=" << win::toUtf8(settings_.loopbackId) << '\n'
        << "language=" << i18n::code(i18n::language()) << '\n'
        << "autostart=" << settings_.autoStart << '\n'
        << "tray.minimize=" << settings_.minimizeToTray << '\n'
        << "ui.fps=" << settings_.uiFps << '\n'
        << "ui.slow_background=" << settings_.uiSlowInBackground << '\n'
        << "ptt.enabled=" << settings_.pttEnabled << '\n'
        << "ptt.toggle=" << settings_.pttToggle << '\n'
        << "midi.device=" << settings_.midiDevice << '\n'
        << "update.check=" << settings_.updateCheck << '\n'
        << "update.latest=" << settings_.updateLatest << '\n'
        << "update.ignored=" << settings_.updateIgnored << '\n'
        << "update.last=" << settings_.updateLastCheck << '\n';
    for (const auto& entry : settings_.midiMap.entries()) out << "midimap=" << midi::MidiMap::serialize(entry) << '\n';
    for (const auto& [name, alias] : settings_.inputAliases) out << "alias.in=" << escapeSetting(name) << '|' << escapeSetting(alias) << '\n';
    for (const auto& [name, alias] : settings_.outputAliases) out << "alias.out=" << escapeSetting(name) << '|' << escapeSetting(alias) << '\n';
    routing_.save(out);
}

// ---- Virtual driver registration ------------------------------------------------------------

void App::registerVirtualDriver(bool install) {
    // Runs this executable elevated; see Driver.h for why it never registers a DLL from a
    // user-writable folder.
    std::string error;
    const bool ok = driver::runElevated(install, window_, error);
    driverStatus_ = driver::status();
    log_.write("Virtual ASIO driver %s: %s -> %s", install ? "register" : "unregister", ok ? "done" : error.c_str(),
               driverStatus_ == driver::Status::Registered        ? "registered"
               : driverStatus_ == driver::Status::NotRegistered ? "not registered"
                                                                 : "registered from another build");
    if (!ok) {
        error_ = error;
    } else if (install && driverStatus_ != driver::Status::Registered) {
        error_ = tr("Driver registration failed.");
    }
}

} // namespace afm::app
