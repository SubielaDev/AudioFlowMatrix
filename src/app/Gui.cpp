// ImGui drawing for App. Every widget edits the RoutingModel directly and sets
// routingDirty_, which republishes the coefficients to the audio thread once per frame.

#include "App.h"
#include "Brand.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace afm::app {

namespace {

// Brand cyan and violet first, then distinct hues for further devices.
constexpr ImU32 kGroupColors[] = {IM_COL32(30, 200, 245, 255), IM_COL32(160, 120, 250, 255),
                                  IM_COL32(240, 170, 70, 255), IM_COL32(90, 210, 140, 255),
                                  IM_COL32(235, 110, 190, 255)};

ImU32 groupColor(std::size_t g) { return kGroupColors[g % std::size(kGroupColors)]; }

ImU32 withAlpha(ImU32 c, int alpha) { return (c & 0x00FFFFFFu) | (static_cast<ImU32>(alpha) << 24); }

std::size_t groupOf(const std::vector<ChannelGroup>& groups, std::uint32_t ch) {
    for (std::size_t g = 0; g < groups.size(); ++g) {
        if (ch >= groups[g].begin && ch < groups[g].begin + groups[g].count) return g;
    }
    return 0;
}

float scale() { return ImGui::GetFontSize() / 16.0f; }

void statusDot(bool ok, bool active) {
    const ImVec4 color = !active ? ImVec4(0.45f, 0.45f, 0.45f, 1) : ok ? ImVec4(0.3f, 0.85f, 0.4f, 1) : ImVec4(0.95f, 0.65f, 0.2f, 1);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float r = ImGui::GetFontSize() * 0.3f;
    dl->AddCircleFilled(ImVec2(p.x + r, p.y + ImGui::GetFrameHeight() * 0.5f), r, ImGui::GetColorU32(color));
    ImGui::Dummy(ImVec2(r * 2.5f, ImGui::GetFrameHeight()));
    ImGui::SameLine();
}

// Vertical peak meter, -60..+6 dBFS.
void drawMeter(const Meter& m, ImVec2 size) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const ImVec2 p1(p0.x + size.x, p0.y + size.y);
    dl->AddRectFilled(p0, p1, IM_COL32(20, 22, 26, 255), 2.0f);

    auto yOf = [&](float db) {
        const float t = std::clamp((db + 60.0f) / 66.0f, 0.0f, 1.0f);
        return p1.y - t * size.y;
    };
    const float level = m.levelDb;
    struct Zone { float from, to; ImU32 color; };
    const Zone zones[] = {{-60.0f, -12.0f, IM_COL32(60, 190, 90, 255)},
                          {-12.0f, -3.0f, IM_COL32(220, 200, 60, 255)},
                          {-3.0f, 6.0f, IM_COL32(230, 70, 60, 255)}};
    for (const Zone& z : zones) {
        if (level <= z.from) break;
        dl->AddRectFilled(ImVec2(p0.x + 1, yOf(std::min(level, z.to))), ImVec2(p1.x - 1, yOf(z.from)), z.color);
    }
    if (m.holdDb > -60.0f) {
        const float y = yOf(m.holdDb);
        dl->AddLine(ImVec2(p0.x + 1, y), ImVec2(p1.x - 1, y), m.holdDb > -0.1f ? IM_COL32(255, 80, 70, 255) : IM_COL32(230, 230, 230, 200), 2.0f);
    }
    dl->AddLine(ImVec2(p0.x, yOf(0.0f)), ImVec2(p0.x + size.x * 0.35f, yOf(0.0f)), IM_COL32(255, 255, 255, 90));
    ImGui::Dummy(size);
}

void horizontalMeter(const Meter& m, float width, float height) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const float t = std::clamp((m.levelDb + 60.0f) / 66.0f, 0.0f, 1.0f);
    dl->AddRectFilled(p0, ImVec2(p0.x + width, p0.y + height), IM_COL32(20, 22, 26, 255));
    const ImU32 color = m.levelDb > -3.0f ? IM_COL32(230, 70, 60, 255) : m.levelDb > -12.0f ? IM_COL32(220, 200, 60, 255) : IM_COL32(60, 190, 90, 255);
    dl->AddRectFilled(p0, ImVec2(p0.x + width * t, p0.y + height), color);
    ImGui::Dummy(ImVec2(width, height));
}

const char* deviceLabel(const std::vector<wasapi::DeviceInfo>& devices, const std::wstring& id) {
    for (const auto& d : devices) {
        if (d.id == id) return d.name.c_str();
    }
    return id.empty() ? "(none)" : "(not connected)";
}

bool deviceCombo(const char* label, const std::vector<wasapi::DeviceInfo>& devices, std::wstring& id) {
    bool changed = false;
    if (ImGui::BeginCombo(label, deviceLabel(devices, id))) {
        for (const auto& d : devices) {
            if (ImGui::Selectable(d.name.c_str(), d.id == id)) {
                id = d.id;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

} // namespace

// ---- Toolbar ------------------------------------------------------------------------------

void App::drawToolbar() {
    const float s = scale();
    const float frameH = ImGui::GetFrameHeight();
    const float logoH = frameH * 1.6f;
    const float y0 = ImGui::GetCursorPosY();
    brand::wordmark(logoH);
    ImGui::SameLine(0, 24 * s);
    ImGui::SetCursorPosY(y0 + (logoH - frameH) * 0.5f);
    ImGui::BeginGroup();

    if (running_) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.18f, 0.16f, 1));
        if (ImGui::Button("Stop", ImVec2(80 * s, 0))) stopAudio();
        ImGui::PopStyleColor();
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.45f, 0.25f, 1));
        if (ImGui::Button("Start", ImVec2(80 * s, 0))) startAudio();
        ImGui::PopStyleColor();
    }

    ImGui::SameLine(0, 16 * s);
    statusDot(running_, true);
    ImGui::AlignTextToFramePadding();
    if (running_) {
        ImGui::Text("%s  |  %.0f Hz  |  %u samples (%.2f ms)", host_.driverName().c_str(), host_.sampleRate(),
                    host_.blockFrames(), host_.blockFrames() * 1000.0 / host_.sampleRate());
    } else {
        ImGui::TextDisabled("%s  (stopped)", settings_.driverName.empty() ? "No driver selected" : settings_.driverName.c_str());
    }

    ImGui::SameLine(0, 24 * s);
    char load[32];
    std::snprintf(load, sizeof(load), "DSP %.0f%%", dspLoad_ * 100.0f);
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, dspPeak_ > 0.8f ? ImVec4(0.85f, 0.3f, 0.25f, 1) : ImGui::GetStyle().Colors[ImGuiCol_PlotHistogram]);
    ImGui::ProgressBar(std::clamp(dspLoad_, 0.0f, 1.0f), ImVec2(110 * s, 0), load);
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Engine time per block / block period.\nPeak: %.0f%%   Driver overloads: %llu", dspPeak_ * 100.0f,
                          static_cast<unsigned long long>(host_.overloadCount()));
    }

    ImGui::SameLine(0, 24 * s);
    const bool client = vasio_.clientConnected();
    statusDot(client, running_);
    ImGui::AlignTextToFramePadding();
    if (client) {
        ImGui::Text("ASIO client connected (PID %u)", vasio_.clientPid());
    } else {
        ImGui::TextDisabled(driverRegistered_ ? "ASIO client: waiting for Traktor/DAW" : "Virtual ASIO driver not registered");
    }
    ImGui::EndGroup();
    ImGui::Spacing();
}

// ---- Matrix -------------------------------------------------------------------------------

void App::drawMatrix() {
    const std::uint32_t nIn = routing_.numInputs();
    const std::uint32_t nOut = routing_.numOutputs();
    if (nIn == 0 || nOut == 0) {
        ImGui::TextDisabled("Start the audio engine to see the routing matrix.");
        return;
    }
    ImGui::TextDisabled("Click a cell to route or unroute. Right-click it for gain, mute and pan.");

    const float s = scale();
    const ImGuiTableFlags flags = ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV |
                                  ImGuiTableFlags_BordersOuter | ImGuiTableFlags_SizingFixedFit |
                                  ImGuiTableFlags_HighlightHoveredColumn;
    if (!ImGui::BeginTable("matrix", static_cast<int>(nOut) + 1, flags)) return;

    ImGui::TableSetupColumn("Inputs  \\  Outputs", ImGuiTableColumnFlags_NoHide | ImGuiTableColumnFlags_WidthFixed, 210 * s);
    for (std::uint32_t o = 0; o < nOut; ++o) {
        ImGui::TableSetupColumn(routing_.outputName(o).c_str(),
                                ImGuiTableColumnFlags_AngledHeader | ImGuiTableColumnFlags_WidthFixed, 30 * s);
    }
    ImGui::TableSetupScrollFreeze(1, 2);
    ImGui::TableAngledHeadersRow();
    ImGui::TableHeadersRow();

    for (std::uint32_t i = 0; i < nIn; ++i) {
        ImGui::TableNextRow();
        const std::size_t ig = groupOf(inputGroups_, i);

        ImGui::TableSetColumnIndex(0);
        ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, withAlpha(groupColor(ig), 28));
        ImGui::PushStyleColor(ImGuiCol_Text, groupColor(ig));
        ImGui::TextUnformatted(routing_.inputName(i).c_str());
        ImGui::PopStyleColor();
        ImGui::SameLine(150 * s);
        horizontalMeter(inputMeters_[i], 52 * s, ImGui::GetTextLineHeight() * 0.6f);

        for (std::uint32_t o = 0; o < nOut; ++o) {
            ImGui::TableSetColumnIndex(static_cast<int>(o) + 1);
            if (groupOf(outputGroups_, o) % 2 == 1) {
                ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, IM_COL32(255, 255, 255, 8));
            }
            Crosspoint& xp = routing_.at(o, i);
            char label[24];
            if (!xp.routed) std::snprintf(label, sizeof(label), "###c");
            else if (xp.mute) std::snprintf(label, sizeof(label), "M###c");
            else if (std::fabs(xp.gainDb) < 0.05f) std::snprintf(label, sizeof(label), "0###c");
            else if (xp.gainDb <= kFaderMinDb) std::snprintf(label, sizeof(label), "-###c");
            else std::snprintf(label, sizeof(label), "%+.0f###c", xp.gainDb);

            // Active routes wear the logo gradient; the button on top is translucent so the label stays readable.
            const bool live = xp.routed && !xp.mute;
            if (live) {
                const ImVec2 p0 = ImGui::GetCursorScreenPos();
                brand::gradientRect(ImGui::GetWindowDrawList(), p0,
                                    ImVec2(p0.x + ImGui::GetContentRegionAvail().x, p0.y + ImGui::GetFrameHeight()));
            }
            const ImVec4 bg = live      ? ImVec4(0, 0, 0, 0)
                              : xp.mute ? ImVec4(0.45f, 0.10f, 0.14f, 1)
                                        : ImVec4(0.07f, 0.09f, 0.13f, 1);
            ImGui::PushID(static_cast<int>(o * nIn + i));
            ImGui::PushStyleColor(ImGuiCol_Button, bg);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, live ? ImVec4(1, 1, 1, 0.18f) : ImVec4(bg.x + 0.1f, bg.y + 0.1f, bg.z + 0.12f, 1));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, live ? ImVec4(1, 1, 1, 0.3f) : ImVec4(bg.x + 0.18f, bg.y + 0.18f, bg.z + 0.2f, 1));
            if (ImGui::Button(label, ImVec2(-FLT_MIN, 0))) {
                xp.routed = !xp.routed;
                routingDirty_ = true;
            }
            ImGui::PopStyleColor(3);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                ImGui::SetTooltip("%s  ->  %s\n%s, %+.1f dB%s", routing_.inputName(i).c_str(), routing_.outputName(o).c_str(),
                                  xp.routed ? "routed" : "not routed", xp.gainDb, xp.mute ? ", muted" : "");
            }
            if (ImGui::BeginPopupContextItem("xp")) {
                ImGui::Text("%s  ->  %s", routing_.inputName(i).c_str(), routing_.outputName(o).c_str());
                ImGui::Separator();
                bool changed = ImGui::Checkbox("Routed", &xp.routed);
                ImGui::SetNextItemWidth(220 * s);
                changed |= ImGui::SliderFloat("Gain", &xp.gainDb, kFaderMinDb, kFaderMaxDb,
                                              xp.gainDb <= kFaderMinDb ? "-inf dB" : "%.1f dB");
                changed |= ImGui::Checkbox("Mute", &xp.mute);
                if (routing_.outputSide(o) != PanSide::Mono) {
                    ImGui::SetNextItemWidth(220 * s);
                    changed |= ImGui::SliderFloat("Pan", &xp.pan, -1.0f, 1.0f, "%.2f");
                }
                if (ImGui::Button("Reset")) {
                    xp.gainDb = 0.0f;
                    xp.pan = 0.0f;
                    xp.mute = false;
                    changed = true;
                }
                if (changed) routingDirty_ = true;
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
    }
    ImGui::EndTable();
}

// ---- Mixer --------------------------------------------------------------------------------

bool App::drawStrip(const char* id, const std::string& name, ChannelStrip& strip, const Meter& meter, unsigned color) {
    const float s = scale();
    const float width = 66 * s;
    const float height = std::max(120.0f, ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing() * 3.2f);
    bool changed = false;

    ImGui::PushID(id);
    ImGui::BeginGroup();

    // Name, clipped to the strip width.
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float lineH = ImGui::GetTextLineHeight();
    dl->AddRectFilled(p, ImVec2(p.x + width, p.y + 3 * s), color);
    dl->PushClipRect(p, ImVec2(p.x + width, p.y + lineH + 6 * s), true);
    dl->AddText(ImVec2(p.x, p.y + 5 * s), IM_COL32(220, 220, 225, 255), name.c_str());
    dl->PopClipRect();
    ImGui::Dummy(ImVec2(width, lineH + 6 * s));
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", name.c_str());

    changed |= ImGui::VSliderFloat("##fader", ImVec2(width * 0.55f, height), &strip.gainDb, kFaderMinDb, kFaderMaxDb, "");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Double-click: 0 dB");
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            strip.gainDb = 0.0f;
            changed = true;
        }
    }
    ImGui::SameLine(0, 4 * s);
    drawMeter(meter, ImVec2(width * 0.45f - 4 * s, height));

    char gain[16];
    if (strip.gainDb <= kFaderMinDb) std::snprintf(gain, sizeof(gain), "-inf");
    else std::snprintf(gain, sizeof(gain), "%+.1f", strip.gainDb);
    const float tw = ImGui::CalcTextSize(gain).x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (width - tw) * 0.5f);
    ImGui::TextUnformatted(gain);

    ImGui::PushStyleColor(ImGuiCol_Button, strip.mute ? ImVec4(0.75f, 0.2f, 0.18f, 1) : ImGui::GetStyle().Colors[ImGuiCol_Button]);
    if (ImGui::Button("MUTE", ImVec2(width, 0))) {
        strip.mute = !strip.mute;
        changed = true;
    }
    ImGui::PopStyleColor();

    ImGui::EndGroup();
    ImGui::PopID();
    return changed;
}

void App::drawMixer() {
    if (routing_.numInputs() == 0) {
        ImGui::TextDisabled("Start the audio engine to see the mixer.");
        return;
    }
    const float s = scale();
    const float half = (ImGui::GetContentRegionAvail().y - ImGui::GetStyle().ItemSpacing.y) * 0.5f;

    auto section = [&](const char* id, const char* title, const std::vector<ChannelGroup>& groups, bool inputs) {
        if (ImGui::BeginChild(id, ImVec2(0, half), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar)) {
            ImGui::TextDisabled("%s", title);
            for (std::size_t g = 0; g < groups.size(); ++g) {
                const ChannelGroup& group = groups[g];
                if (g > 0) ImGui::SameLine(0, 18 * s);
                ImGui::BeginGroup();
                ImGui::PushStyleColor(ImGuiCol_Text, groupColor(g));
                ImGui::TextUnformatted(group.name.c_str());
                ImGui::PopStyleColor();
                for (std::uint32_t k = 0; k < group.count; ++k) {
                    const std::uint32_t ch = group.begin + k;
                    if (k > 0) ImGui::SameLine(0, 6 * s);
                    char stripId[16];
                    std::snprintf(stripId, sizeof(stripId), "%c%u", inputs ? 'i' : 'o', ch);
                    if (inputs) {
                        routingDirty_ |= drawStrip(stripId, routing_.inputName(ch), routing_.input(ch), inputMeters_[ch], groupColor(g));
                    } else {
                        routingDirty_ |= drawStrip(stripId, routing_.outputName(ch), routing_.output(ch), outputMeters_[ch], groupColor(g));
                    }
                }
                ImGui::EndGroup();
            }
        }
        ImGui::EndChild();
    };
    section("inputs", "INPUTS", inputGroups_, true);
    section("outputs", "OUTPUT BUSES", outputGroups_, false);
}

// ---- Devices ------------------------------------------------------------------------------

void App::drawDevices() {
    const float s = scale();
    const float comboWidth = 380 * s;
    bool changed = false;

    ImGui::SeparatorText("Master ASIO interface (clock source)");
    ImGui::SetNextItemWidth(comboWidth);
    if (ImGui::BeginCombo("Driver", settings_.driverName.empty() ? "(none)" : settings_.driverName.c_str())) {
        for (const auto& d : drivers_) {
            if (ImGui::Selectable(d.name.c_str(), d.name == settings_.driverName)) {
                settings_.driverName = d.name;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    if (drivers_.empty()) ImGui::TextColored(ImVec4(0.95f, 0.65f, 0.2f, 1), "No ASIO drivers found. Install your interface's ASIO driver.");

    static const double kRates[] = {44100.0, 48000.0, 88200.0, 96000.0};
    char rateLabel[32];
    std::snprintf(rateLabel, sizeof(rateLabel), "%.0f Hz", settings_.sampleRate);
    ImGui::SetNextItemWidth(comboWidth);
    if (ImGui::BeginCombo("Sample rate", rateLabel)) {
        for (double r : kRates) {
            char item[32];
            std::snprintf(item, sizeof(item), "%.0f Hz", r);
            if (ImGui::Selectable(item, r == settings_.sampleRate)) {
                settings_.sampleRate = r;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::BeginDisabled(!running_);
    if (ImGui::Button("Open driver control panel")) host_.openControlPanel();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("Buffer size is set in the interface's own control panel.");

    ImGui::SeparatorText("Virtual ASIO driver for Traktor / DAWs");
    if (driverRegistered_) {
        ImGui::TextColored(ImVec4(0.3f, 0.85f, 0.4f, 1), "\"AudioFlow Matrix ASIO\" is registered.");
        ImGui::SameLine();
        if (ImGui::SmallButton("Unregister")) registerVirtualDriver(false);
    } else {
        ImGui::TextColored(ImVec4(0.95f, 0.65f, 0.2f, 1), "Not registered.");
        ImGui::SameLine();
        if (ImGui::Button("Register driver (administrator)")) registerVirtualDriver(true);
    }
    ImGui::TextDisabled("In Traktor: Preferences > Audio Setup > Audio Device: \"AudioFlow Matrix ASIO\". "
                        "Start AudioFlow Matrix first.");
    if (vasio_.clientConnected()) {
        ImGui::Text("Client PID %u  |  %llu blocks  |  %llu frames of underrun", vasio_.clientPid(),
                    static_cast<unsigned long long>(vasio_.clientBlocks()),
                    static_cast<unsigned long long>(vasio_.clientUnderruns()));
    }

    ImGui::SeparatorText("USB microphone (WASAPI)");
    changed |= ImGui::Checkbox("Enable##mic", &settings_.micEnabled);
    ImGui::SameLine();
    changed |= ImGui::Checkbox("Exclusive mode##mic", &settings_.micExclusive);
    ImGui::SetNextItemWidth(comboWidth);
    changed |= deviceCombo("Capture device", captureDevices_, settings_.micId);
    if (mic_) {
        const auto& info = mic_->info();
        ImGui::Text("%s  |  drift %+.1f ppm  |  %s  |  dropouts %llu", info.format.c_str(), mic_->resampler().correctionPpm(),
                    mic_->resampler().locked() ? "locked" : "priming",
                    static_cast<unsigned long long>(mic_->resampler().dropouts()));
        if (mic_->deviceLost()) ImGui::TextColored(ImVec4(0.95f, 0.4f, 0.3f, 1), "Device disconnected. Press Apply to reopen.");
    }
    changed |= drawVoiceControls();

    ImGui::SeparatorText("Stream output for Discord / OBS (WASAPI)");
    changed |= ImGui::Checkbox("Enable##stream", &settings_.streamEnabled);
    ImGui::SameLine();
    changed |= ImGui::Checkbox("Exclusive mode##stream", &settings_.streamExclusive);
    ImGui::SetNextItemWidth(comboWidth);
    changed |= deviceCombo("Render device", renderDevices_, settings_.streamId);
    ImGui::TextDisabled("Tip: with a virtual cable (e.g. VB-CABLE) pick \"CABLE Input\" here and "
                        "\"CABLE Output\" as the microphone in Discord / OBS.");
    if (stream_) {
        const auto& info = stream_->info();
        ImGui::Text("%s  |  drift %+.1f ppm  |  %s  |  dropouts %llu", info.format.c_str(), stream_->resampler().correctionPpm(),
                    stream_->resampler().locked() ? "locked" : "priming",
                    static_cast<unsigned long long>(stream_->resampler().dropouts()));
        if (stream_->deviceLost()) ImGui::TextColored(ImVec4(0.95f, 0.4f, 0.3f, 1), "Device disconnected. Press Apply to reopen.");
    }

    ImGui::SeparatorText("General");
    changed |= ImGui::Checkbox("Start audio when AudioFlow Matrix opens", &settings_.autoStart);
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.45f, 0.25f, 1));
    if (ImGui::Button("Apply and restart audio", ImVec2(220 * s, 0))) {
        saveSettings();
        startAudio();
    }
    ImGui::PopStyleColor();
    ImGui::SameLine();
    if (ImGui::Button("Refresh device lists")) refreshDevices();
    ImGui::SameLine();
    if (ImGui::Button("Clear all routes")) {
        routing_.clearRoutes();
        routingDirty_ = true;
    }

    if (changed) settingsDirty_ = true;
}

// ---- Voice processing (mic) --------------------------------------------------------------

// Denoise / gate settings apply live: the capture thread reads them from atomics.
bool App::drawVoiceControls() {
    const float s = scale();
    bool changed = false;
    dsp::VoiceProcessor* voice = mic_ ? &mic_->voice() : nullptr;
    const bool denoiseAvailable = voice == nullptr || voice->denoiseAvailable();

    ImGui::BeginDisabled(!denoiseAvailable);
    if (ImGui::Checkbox("Denoise (RNNoise)", &settings_.micDenoise)) {
        changed = true;
        if (voice) voice->setDenoise(settings_.micDenoise);
    }
    ImGui::EndDisabled();
    ImGui::SameLine(0, 20 * s);
    if (ImGui::Checkbox("Gate", &settings_.micGate)) {
        changed = true;
        if (voice) voice->setGate(settings_.micGate);
    }
    ImGui::SameLine(0, 20 * s);
    ImGui::SetNextItemWidth(220 * s);
    if (ImGui::SliderFloat("Threshold", &settings_.micGateThresholdDb, dsp::NoiseGate::kMinThresholdDb,
                           dsp::NoiseGate::kMaxThresholdDb, "%.0f dB")) {
        changed = true;
        if (voice) voice->setGateThresholdDb(settings_.micGateThresholdDb);
    }

    if (voice == nullptr) return changed;
    if (!denoiseAvailable) {
        ImGui::TextColored(ImVec4(0.95f, 0.65f, 0.2f, 1), "Denoise needs the mic at 48000 Hz; this device runs at %.0f Hz.",
                           mic_->info().deviceRate);
    }
    statusDot(voice->gateOpen() || !settings_.micGate, true);
    ImGui::AlignTextToFramePadding();
    if (settings_.micGate) {
        const float gain = voice->gateGain();
        const float gainDb = gain > 1e-3f ? 20.0f * std::log10(gain) : -60.0f;
        ImGui::Text("Gate %s (%.0f dB)", voice->gateOpen() ? "open" : "closed", gainDb);
    } else {
        ImGui::TextDisabled("Gate off");
    }
    if (denoiseAvailable) {
        ImGui::SameLine(0, 24 * s);
        ImGui::Text("Voice %3.0f%%", voice->voiceProbability() * 100.0f);
        ImGui::SameLine(0, 24 * s);
        ImGui::TextDisabled("+%.0f ms latency", voice->latencyFrames() * 1000.0 / mic_->info().deviceRate);
    }
    return changed;
}

// ---- Status bar ---------------------------------------------------------------------------

void App::drawStatusBar() {
    ImGui::Separator();
    if (!error_.empty()) {
        ImGui::TextColored(ImVec4(0.95f, 0.4f, 0.3f, 1), "%s", error_.c_str());
    } else if (!warnings_.empty()) {
        ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.3f, 1), "%s", warnings_.front().c_str());
        if (warnings_.size() > 1 && ImGui::IsItemHovered()) {
            std::string all;
            for (const auto& w : warnings_) all += w + "\n";
            ImGui::SetTooltip("%s", all.c_str());
        }
    } else {
        ImGui::TextDisabled("%s  |  %u x %u matrix  |  mixer: %s", running_ ? "Running" : "Stopped", routing_.numInputs(),
                            routing_.numOutputs(), dsp::MatrixMixer::usingAvx2() ? "AVX2/FMA" : "scalar");
    }
}

} // namespace afm::app
