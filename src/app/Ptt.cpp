// Push-to-talk (mic -> stream) and its MIDI input: device handling, MIDI Learn, the
// diagnostics log and the UI. The gate itself runs in the engine (AudioEngine::setPtt).

#include "App.h"

#include "imgui.h"

#include <shellapi.h>

#include <algorithm>

namespace afm::app {

namespace {

constexpr std::size_t kMonitorLines = 12;

const char* actionText(midi::MidiBinding::Action a) {
    switch (a) {
    case midi::MidiBinding::Action::Press: return "  -> PTT press";
    case midi::MidiBinding::Action::Release: return "  -> PTT release";
    default: return "";
    }
}

} // namespace

// ---- MIDI thread ----------------------------------------------------------------------------

// Runs on the WinMM thread, also while the window is minimised (the GUI loop is then
// paused): the PTT gate is driven from here directly; logging and Learn go through the
// ring to the GUI thread.
void App::onMidi(void* user, std::uint32_t message, std::uint32_t timeMs) noexcept {
    if ((message & 0xFF) >= 0xF0) return;  // clock, active sensing, ...: neither mappable nor worth logging
    auto* self = static_cast<App*>(user);
    auto action = midi::MidiBinding::Action::None;
    if (!self->midiLearning_.load(std::memory_order_acquire)) {
        const auto binding = midi::MidiBinding::unpack(self->pttBinding_.load(std::memory_order_relaxed));
        action = binding.actionFor(midi::parseShortMessage(message));
        if (action == midi::MidiBinding::Action::Press) self->ptt_->press(PushToTalk::Source::Midi);
        if (action == midi::MidiBinding::Action::Release) self->ptt_->release(PushToTalk::Source::Midi);
    }
    if (!self->midiEvents_.tryPush({message, timeMs, action, self->ptt_->open()})) {
        self->midiDropped_.fetch_add(1, std::memory_order_relaxed);
    }
}

// ---- GUI thread -----------------------------------------------------------------------------

void App::applyPttSettings() {
    ptt_->setEnabled(settings_.pttEnabled);
    if ((ptt_->mode() == PushToTalk::Mode::Toggle) != settings_.pttToggle) {
        ptt_->setMode(settings_.pttToggle ? PushToTalk::Mode::Toggle : PushToTalk::Mode::Hold);
    }
    const auto binding = midi::MidiBinding::parse(settings_.pttMidi);
    pttBinding_.store(binding.pack(), std::memory_order_relaxed);
    log_.write("PTT: %s, mode %s, MIDI mapping %s", settings_.pttEnabled ? "enabled" : "disabled",
               settings_.pttToggle ? "toggle" : "hold", binding.describe().c_str());
}

void App::listMidiDevices() {
    midiDevices_ = midi::MidiInput::list();
    std::string names;
    for (const auto& d : midiDevices_) names += (names.empty() ? "\"" : ", \"") + d.name + "\"";
    log_.write("MIDI inputs found: %zu %s", midiDevices_.size(), names.c_str());
}

void App::openMidi() {
    midiIn_.close();
    ptt_->release(PushToTalk::Source::Midi);  // a pad held while switching ports must not stick
    midiError_.clear();
    if (settings_.midiDevice.empty()) {
        log_.write("MIDI: no input selected");
        return;
    }
    const auto it = std::find_if(midiDevices_.begin(), midiDevices_.end(),
                                 [&](const auto& d) { return d.name == settings_.midiDevice; });
    if (it == midiDevices_.end()) {
        midiError_ = "MIDI input \"" + settings_.midiDevice + "\" is not connected.";
        log_.write("MIDI: %s", midiError_.c_str());
        return;
    }
    if (!midiIn_.open(*it, &App::onMidi, this, midiError_)) {
        log_.write("MIDI: %s", midiError_.c_str());
        return;
    }
    log_.write("MIDI: listening to \"%s\" (port %u)", it->name.c_str(), it->id);
}

void App::pollMidi() {
    MidiLogEntry e;
    while (midiEvents_.tryPop(e)) {
        const std::string text = midi::describe(e.message);
        if (midiLearning_.load(std::memory_order_relaxed)) {
            const auto binding = midi::MidiBinding::learn(midi::parseShortMessage(e.message));
            if (binding.type != midi::MidiBinding::Type::None) {
                settings_.pttMidi = binding.serialize();
                pttBinding_.store(binding.pack(), std::memory_order_relaxed);
                midiLearning_.store(false, std::memory_order_release);
                settingsDirty_ = true;
                log_.write("MIDI Learn: PTT mapped to %s", binding.describe().c_str());
            }
        }
        log_.write("MIDI in   %s   t=%u ms%s   (PTT %s)", text.c_str(), e.timeMs, actionText(e.action),
                   e.pttOpen ? "open" : "closed");
        midiMonitor_.push_front(text + actionText(e.action));
        if (midiMonitor_.size() > kMonitorLines) midiMonitor_.pop_back();
    }
    if (const auto dropped = midiDropped_.exchange(0, std::memory_order_relaxed); dropped > 0) {
        log_.write("MIDI: %llu messages not logged (queue full); PTT still followed them",
                   static_cast<unsigned long long>(dropped));
    }

    // Log every open/close, whichever source caused it.
    const bool passing = ptt_->passing();
    if (passing != pttWasPassing_) {
        pttWasPassing_ = passing;
        if (settings_.pttEnabled) log_.write("PTT: mic -> stream %s", passing ? "OPEN" : "closed");
    }
}

// Toolbar button. Hold: talk while the mouse button is down. Toggle: each click flips.
void App::drawPttButton() {
    if (!settings_.pttEnabled) return;
    const float s = ImGui::GetFontSize() / 16.0f;
    const bool open = ptt_->open();
    const bool learning = midiLearning_.load(std::memory_order_relaxed);

    const ImVec4 color = learning ? ImVec4(0.55f, 0.35f, 0.95f, 1) : open ? ImVec4(0.85f, 0.15f, 0.2f, 1) : ImVec4(0.16f, 0.2f, 0.27f, 1);
    ImGui::PushStyleColor(ImGuiCol_Button, color);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(color.x + 0.08f, color.y + 0.08f, color.z + 0.08f, 1));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(color.x + 0.15f, color.y + 0.15f, color.z + 0.15f, 1));
    ImGui::Button(learning ? "LEARN: press a MIDI control###ptt" : open ? "ON AIR###ptt" : "PTT  (mic muted on stream)###ptt",
                  ImVec2(learning ? 230 * s : 190 * s, 0));
    ImGui::PopStyleColor(3);
    if (ImGui::IsItemActivated()) ptt_->press(PushToTalk::Source::Mouse);
    if (ImGui::IsItemDeactivated()) ptt_->release(PushToTalk::Source::Mouse);

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        const auto binding = midi::MidiBinding::parse(settings_.pttMidi);
        ImGui::SetTooltip("Push-to-talk, mic -> stream (%s)\nMIDI: %s%s\nRight-click: MIDI Learn",
                          settings_.pttToggle ? "click to toggle" : "hold to talk", binding.describe().c_str(),
                          midiIn_.isOpen() ? "" : " (no MIDI input open)");
    }
    if (ImGui::BeginPopupContextItem("ptt-menu")) {
        ImGui::BeginDisabled(!midiIn_.isOpen());
        if (ImGui::MenuItem(learning ? "Cancel MIDI Learn" : "MIDI Learn")) {
            midiLearning_.store(!learning, std::memory_order_release);
            log_.write("MIDI Learn: %s", learning ? "cancelled" : "waiting for a control");
        }
        ImGui::EndDisabled();
        if (!midiIn_.isOpen()) ImGui::TextDisabled("Pick a MIDI input in the Devices tab first.");
        if (ImGui::MenuItem("Clear MIDI mapping")) {
            settings_.pttMidi = "none";
            pttBinding_.store(midi::MidiBinding{}.pack(), std::memory_order_relaxed);
            settingsDirty_ = true;
            log_.write("PTT: MIDI mapping cleared");
        }
        ImGui::EndPopup();
    }
}

// Devices tab section.
bool App::drawPttSettings() {
    const float s = ImGui::GetFontSize() / 16.0f;
    bool changed = false;
    ImGui::SeparatorText("Push-to-talk: mic -> stream (optional)");

    bool pttChanged = ImGui::Checkbox("Enable push-to-talk", &settings_.pttEnabled);
    ImGui::SameLine(0, 24 * s);
    int mode = settings_.pttToggle ? 1 : 0;
    pttChanged |= ImGui::RadioButton("Hold to talk", &mode, 0);
    ImGui::SameLine();
    pttChanged |= ImGui::RadioButton("Toggle", &mode, 1);
    if (pttChanged) {
        settings_.pttToggle = mode == 1;
        applyPttSettings();
        changed = true;
    }
    ImGui::TextDisabled("While enabled, the mic only reaches the Stream output when PTT is open. "
                        "The mic to your interface and everything else is unaffected.");
    if (settings_.pttEnabled && running_ && (mic_ == nullptr || stream_ == nullptr)) {
        ImGui::TextColored(ImVec4(0.95f, 0.65f, 0.2f, 1), "Enable the USB mic and the stream output for PTT to act.");
    }

    // MIDI input port.
    ImGui::SetNextItemWidth(320 * s);
    if (ImGui::BeginCombo("MIDI input", settings_.midiDevice.empty() ? "(none)" : settings_.midiDevice.c_str())) {
        if (ImGui::Selectable("(none)", settings_.midiDevice.empty())) {
            settings_.midiDevice.clear();
            openMidi();
            changed = true;
        }
        for (const auto& d : midiDevices_) {
            if (ImGui::Selectable(d.name.c_str(), d.name == settings_.midiDevice)) {
                settings_.midiDevice = d.name;
                openMidi();
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    // Re-scans the ports and reopens the selected one: picks up a controller plugged in (or
    // unplugged and replugged) while the app was running.
    ImGui::SameLine();
    if (ImGui::Button("Refresh##midi")) {
        log_.write("MIDI: refresh requested");
        listMidiDevices();
        openMidi();
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::SetTooltip("Look for MIDI inputs again and reconnect the selected one.");
    }
    if (midiDevices_.empty()) {
        ImGui::TextDisabled("No MIDI inputs found: connect one and press Refresh.");
    } else if (!settings_.midiDevice.empty() && midiIn_.isOpen()) {
        ImGui::TextColored(ImVec4(0.3f, 0.85f, 0.4f, 1), "Listening to \"%s\".", midiIn_.deviceName().c_str());
    }
    if (!midiError_.empty()) ImGui::TextColored(ImVec4(0.95f, 0.65f, 0.2f, 1), "%s", midiError_.c_str());

    // Mapping + Learn.
    const auto binding = midi::MidiBinding::parse(settings_.pttMidi);
    const bool learning = midiLearning_.load(std::memory_order_relaxed);
    ImGui::AlignTextToFramePadding();
    ImGui::Text("PTT mapped to: %s", binding.describe().c_str());
    ImGui::SameLine(0, 16 * s);
    ImGui::BeginDisabled(!midiIn_.isOpen());
    if (ImGui::Button(learning ? "Cancel Learn" : "MIDI Learn")) {
        midiLearning_.store(!learning, std::memory_order_release);
        log_.write("MIDI Learn: %s", learning ? "cancelled" : "waiting for a control");
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Clear")) {
        settings_.pttMidi = "none";
        pttBinding_.store(midi::MidiBinding{}.pack(), std::memory_order_relaxed);
        log_.write("PTT: MIDI mapping cleared");
        changed = true;
    }
    if (learning) ImGui::TextColored(ImVec4(0.6f, 0.45f, 1.0f, 1), "Press the pad, key or button to use for PTT...");

    // Monitor: proves messages arrive even when the mapping is wrong.
    ImGui::Text("Last MIDI messages%s:", midiIn_.isOpen() ? "" : " (no input open)");
    ImGui::BeginChild("midi-monitor", ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 6.5f), ImGuiChildFlags_Borders);
    if (midiMonitor_.empty()) ImGui::TextDisabled("Nothing received yet.");
    for (const auto& line : midiMonitor_) ImGui::TextUnformatted(line.c_str());
    ImGui::EndChild();

    ImGui::TextDisabled("Diagnostics log (send it if MIDI or PTT misbehave): %%APPDATA%%\\AudioFlowMatrix\\logs");
    ImGui::SameLine();
    if (ImGui::SmallButton("Open log folder") && !log_.directory().empty()) {
        ShellExecuteW(nullptr, L"explore", log_.directory().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
    return changed;
}

} // namespace afm::app
