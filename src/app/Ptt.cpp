// Push-to-talk (mic -> stream) and MIDI control: the input port, the MIDI map (MIDI Learn
// for PTT and channel mutes), the diagnostics log and their UI. The PTT gate itself runs
// in the engine (AudioEngine::setPtt).

#include "App.h"

#include "imgui.h"

#include <shellapi.h>

#include <algorithm>

namespace afm::app {

namespace {

constexpr std::size_t kMonitorLines = 12;

const char* pttActionText(midi::MidiBinding::Action a) {
    switch (a) {
    case midi::MidiBinding::Action::Press: return "  -> PTT press";
    case midi::MidiBinding::Action::Release: return "  -> PTT release";
    default: return "";
    }
}

bool startsWith(const std::string& s, const std::string& prefix) { return s.compare(0, prefix.size(), prefix) == 0; }

// "Mute: Mic L (input)" etc., for the log and the mapping list.
std::string targetLabel(const std::string& target) {
    if (target == midi::MidiMap::pttTarget()) return "Push-to-talk";
    const std::string in = midi::MidiMap::inputMuteTarget("");
    const std::string out = midi::MidiMap::outputMuteTarget("");
    if (startsWith(target, in)) return "Mute: " + target.substr(in.size()) + " (input)";
    if (startsWith(target, out)) return "Mute: " + target.substr(out.size()) + " (output)";
    return target;
}

} // namespace

// ---- MIDI thread ----------------------------------------------------------------------------

// Runs on the WinMM thread, also while the window is minimised. The PTT gate is driven from
// here directly (it has to react within milliseconds); everything else (mutes, Learn,
// logging) goes through the ring to the GUI thread, which also runs while minimised via idle().
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

void App::syncPttBinding() {
    pttBinding_.store(settings_.midiMap.bindingFor(midi::MidiMap::pttTarget()).pack(), std::memory_order_relaxed);
}

void App::applyPttSettings() {
    ptt_->setEnabled(settings_.pttEnabled);
    if ((ptt_->mode() == PushToTalk::Mode::Toggle) != settings_.pttToggle) {
        ptt_->setMode(settings_.pttToggle ? PushToTalk::Mode::Toggle : PushToTalk::Mode::Hold);
    }
    syncPttBinding();
    log_.write("PTT: %s, mode %s, MIDI mapping %s", settings_.pttEnabled ? "enabled" : "disabled",
               settings_.pttToggle ? "toggle" : "hold",
               settings_.midiMap.bindingFor(midi::MidiMap::pttTarget()).describe().c_str());
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

void App::startMidiLearn(const std::string& target) {
    midiLearnTarget_ = target;
    midiLearning_.store(true, std::memory_order_release);
    log_.write("MIDI Learn: waiting for a control for %s", targetLabel(target).c_str());
}

void App::cancelMidiLearn() {
    if (!midiLearning_.exchange(false, std::memory_order_acq_rel)) return;
    log_.write("MIDI Learn: cancelled");
    midiLearnTarget_.clear();
}

void App::clearMidiMapping(const std::string& target) {
    settings_.midiMap.clear(target);
    syncPttBinding();
    settingsDirty_ = true;
    log_.write("MIDI: mapping for %s cleared", targetLabel(target).c_str());
}

// Mutes toggle on press, like a click on the MUTE button; releases are ignored.
void App::applyMidiAction(const std::string& target, const midi::MidiEvent& event) {
    const auto binding = settings_.midiMap.bindingFor(target);
    if (binding.actionFor(event) != midi::MidiBinding::Action::Press) return;

    const std::string in = midi::MidiMap::inputMuteTarget("");
    const std::string out = midi::MidiMap::outputMuteTarget("");
    const bool isInput = startsWith(target, in);
    if (!isInput && !startsWith(target, out)) return;
    const std::string name = target.substr(isInput ? in.size() : out.size());
    const std::uint32_t count = isInput ? routing_.numInputs() : routing_.numOutputs();
    for (std::uint32_t ch = 0; ch < count; ++ch) {
        if ((isInput ? routing_.inputName(ch) : routing_.outputName(ch)) != name) continue;
        ChannelStrip& strip = isInput ? routing_.input(ch) : routing_.output(ch);
        strip.mute = !strip.mute;
        routingDirty_ = true;
        log_.write("MIDI -> %s: %s", targetLabel(target).c_str(), strip.mute ? "MUTED" : "unmuted");
        return;
    }
    log_.write("MIDI -> %s: channel not active right now (start audio / enable the device)", targetLabel(target).c_str());
}

void App::pollMidi() {
    MidiLogEntry e;
    while (midiEvents_.tryPop(e)) {
        const midi::MidiEvent event = midi::parseShortMessage(e.message);
        std::string text = midi::describe(e.message);

        if (midiLearning_.load(std::memory_order_relaxed)) {
            const auto binding = midi::MidiBinding::learn(event);
            if (binding.type != midi::MidiBinding::Type::None && !midiLearnTarget_.empty()) {
                const std::string* previous = settings_.midiMap.targetFor(event);
                if (previous != nullptr && *previous != midiLearnTarget_) {
                    log_.write("MIDI Learn: %s was mapped to %s; moved", binding.describe().c_str(),
                               targetLabel(*previous).c_str());
                }
                settings_.midiMap.assign(midiLearnTarget_, binding);
                syncPttBinding();
                settingsDirty_ = true;
                log_.write("MIDI Learn: %s mapped to %s", targetLabel(midiLearnTarget_).c_str(), binding.describe().c_str());
                midiLearnTarget_.clear();
                midiLearning_.store(false, std::memory_order_release);
                text += "  -> learned";
            }
        } else if (const std::string* target = settings_.midiMap.targetFor(event);
                   target != nullptr && *target != midi::MidiMap::pttTarget() &&
                   settings_.midiMap.bindingFor(*target).actionFor(event) == midi::MidiBinding::Action::Press) {
            text += "  -> " + targetLabel(*target);
            applyMidiAction(*target, event);
        }
        text += pttActionText(e.action);

        log_.write("MIDI in   %s   t=%u ms   (PTT %s)", text.c_str(), e.timeMs, e.pttOpen ? "open" : "closed");
        midiMonitor_.push_front(text);
        if (midiMonitor_.size() > kMonitorLines) midiMonitor_.pop_back();
    }
    if (const auto dropped = midiDropped_.exchange(0, std::memory_order_relaxed); dropped > 0) {
        log_.write("MIDI: %llu messages not logged (queue full); PTT still followed them",
                   static_cast<unsigned long long>(dropped));
    }

    // Log every PTT open/close, whichever source caused it.
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

    const ImVec4 color = open ? ImVec4(0.85f, 0.15f, 0.2f, 1) : ImVec4(0.16f, 0.2f, 0.27f, 1);
    ImGui::PushStyleColor(ImGuiCol_Button, color);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(color.x + 0.08f, color.y + 0.08f, color.z + 0.08f, 1));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(color.x + 0.15f, color.y + 0.15f, color.z + 0.15f, 1));
    ImGui::Button(open ? "ON AIR###ptt" : "PTT  (mic muted on stream)###ptt", ImVec2(190 * s, 0));
    ImGui::PopStyleColor(3);
    if (ImGui::IsItemActivated()) ptt_->press(PushToTalk::Source::Mouse);
    if (ImGui::IsItemDeactivated()) ptt_->release(PushToTalk::Source::Mouse);

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::SetTooltip("Push-to-talk, mic -> stream (%s)\nMIDI: %s (map it in the MIDI tab)",
                          settings_.pttToggle ? "click to toggle" : "hold to talk",
                          settings_.midiMap.bindingFor(midi::MidiMap::pttTarget()).describe().c_str());
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
    ImGui::TextDisabled("MIDI: %s. Map it, and channel mutes, in the MIDI tab.",
                        settings_.midiMap.bindingFor(midi::MidiMap::pttTarget()).describe().c_str());
    if (settings_.pttEnabled && running_ && (mic_ == nullptr || stream_ == nullptr)) {
        ImGui::TextColored(ImVec4(0.95f, 0.65f, 0.2f, 1), "Enable the USB mic and the stream output for PTT to act.");
    }
    return changed;
}

// MIDI tab: input port, every mappable control with Learn / Clear, monitor and log.
void App::drawMidi() {
    const float s = ImGui::GetFontSize() / 16.0f;
    bool changed = false;

    ImGui::SeparatorText("MIDI input");
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
    if (!midiError_.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.65f, 0.2f, 1));
        ImGui::TextWrapped("%s", midiError_.c_str());
        ImGui::PopStyleColor();
    }

    // Every mappable control. Channels come from the current layout; mappings for channels
    // that are not active right now are listed too, so they can still be cleared.
    ImGui::SeparatorText("Mappable controls");
    const bool learning = midiLearning_.load(std::memory_order_relaxed);
    if (learning) {
        ImGui::TextColored(ImVec4(0.6f, 0.45f, 1.0f, 1), "Press the pad, key or button for \"%s\"...",
                           targetLabel(midiLearnTarget_).c_str());
    } else if (!midiIn_.isOpen()) {
        ImGui::TextDisabled("Pick a MIDI input above to use MIDI Learn.");
    } else {
        ImGui::TextDisabled("Press Learn on a control, then the pad, key or button that should drive it.");
    }

    struct Row {
        std::string target;
        std::string label;
        bool active;
    };
    std::vector<Row> rows;
    rows.push_back({midi::MidiMap::pttTarget(), "Push-to-talk (mic -> stream)", true});
    for (std::uint32_t i = 0; i < routing_.numInputs(); ++i) {
        rows.push_back({midi::MidiMap::inputMuteTarget(routing_.inputName(i)), "", true});
    }
    for (std::uint32_t o = 0; o < routing_.numOutputs(); ++o) {
        rows.push_back({midi::MidiMap::outputMuteTarget(routing_.outputName(o)), "", true});
    }
    for (const auto& entry : settings_.midiMap.entries()) {
        const bool listed = std::any_of(rows.begin(), rows.end(), [&](const Row& r) { return r.target == entry.target; });
        if (!listed) rows.push_back({entry.target, "", false});
    }
    for (Row& r : rows) {
        if (r.label.empty()) r.label = targetLabel(r.target);
    }

    static ImGuiTextFilter filter;
    filter.Draw("Filter##midi", 260 * s);
    if (routing_.numInputs() == 0) {
        ImGui::SameLine();
        ImGui::TextDisabled("Start audio to list the channel mutes.");
    }

    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY |
                                  ImGuiTableFlags_SizingStretchProp;
    const float tableHeight = std::max(160.0f * s, ImGui::GetContentRegionAvail().y - ImGui::GetTextLineHeightWithSpacing() * 10.5f);
    if (ImGui::BeginTable("midi-map", 3, flags, ImVec2(0, tableHeight))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Control", ImGuiTableColumnFlags_WidthStretch, 3.0f);
        ImGui::TableSetupColumn("MIDI control", ImGuiTableColumnFlags_WidthStretch, 2.0f);
        ImGui::TableSetupColumn("##actions", ImGuiTableColumnFlags_WidthFixed, 170 * s);
        ImGui::TableHeadersRow();
        for (const Row& r : rows) {
            if (!filter.PassFilter(r.label.c_str())) continue;
            const auto binding = settings_.midiMap.bindingFor(r.target);
            const bool learningThis = learning && midiLearnTarget_ == r.target;
            ImGui::PushID(r.target.c_str());
            ImGui::TableNextRow();
            if (learningThis) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(120, 80, 230, 70));

            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            if (r.active) ImGui::TextUnformatted(r.label.c_str());
            else ImGui::TextDisabled("%s  (not active)", r.label.c_str());

            ImGui::TableSetColumnIndex(1);
            ImGui::AlignTextToFramePadding();
            if (learningThis) ImGui::TextColored(ImVec4(0.7f, 0.6f, 1.0f, 1), "waiting...");
            else if (binding.type == midi::MidiBinding::Type::None) ImGui::TextDisabled("-");
            else ImGui::TextUnformatted(binding.describe().c_str());

            ImGui::TableSetColumnIndex(2);
            if (learningThis) {
                if (ImGui::SmallButton("Cancel")) cancelMidiLearn();
            } else {
                ImGui::BeginDisabled(!midiIn_.isOpen());
                if (ImGui::SmallButton("Learn")) startMidiLearn(r.target);
                ImGui::EndDisabled();
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(binding.type == midi::MidiBinding::Type::None);
            if (ImGui::SmallButton("Clear")) clearMidiMapping(r.target);
            ImGui::EndDisabled();
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (learning && ImGui::IsKeyPressed(ImGuiKey_Escape)) cancelMidiLearn();

    // Monitor: proves messages arrive even when nothing is mapped.
    ImGui::Text("Last MIDI messages%s:", midiIn_.isOpen() ? "" : " (no input open)");
    ImGui::BeginChild("midi-monitor", ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 6.5f), ImGuiChildFlags_Borders);
    if (midiMonitor_.empty()) ImGui::TextDisabled("Nothing received yet.");
    for (const auto& line : midiMonitor_) ImGui::TextUnformatted(line.c_str());
    ImGui::EndChild();

    ImGui::TextDisabled("Diagnostics log (send it if MIDI misbehaves): %%APPDATA%%\\AudioFlowMatrix\\logs");
    ImGui::SameLine();
    if (ImGui::SmallButton("Open log folder") && !log_.directory().empty()) {
        ShellExecuteW(nullptr, L"explore", log_.directory().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }

    if (changed) settingsDirty_ = true;
}

} // namespace afm::app
