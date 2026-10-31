// ImGui drawing for App. Every widget edits the RoutingModel directly and sets
// routingDirty_, which republishes the coefficients to the audio thread once per frame.

#include "App.h"
#include "Brand.h"
#include "I18n.h"

#include "imgui.h"
#include "imgui_internal.h"  // ClearActiveID

#include <shellapi.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#ifndef AFM_VERSION
#define AFM_VERSION "0.0.0"
#endif

namespace afm::app {

namespace {

constexpr wchar_t kProjectPage[] = L"https://github.com/SubielaDev/AudioFlowMatrix";
constexpr wchar_t kKofiPage[] = L"https://ko-fi.com/J2A629T99Y";

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

constexpr ImVec2 kGithubMark[] = {
#include "GithubMark.inc"
};

// The GitHub mark `size` pixels square with its top-left corner at `origin`.
void drawGithubMark(ImDrawList* drawList, ImVec2 origin, float size, ImU32 color) {
    ImVec2 points[std::size(kGithubMark)];
    const float k = size / 16.0f;
    for (std::size_t i = 0; i < std::size(kGithubMark); ++i) {
        points[i] = ImVec2(origin.x + kGithubMark[i].x * k, origin.y + kGithubMark[i].y * k);
    }
    drawList->AddConcavePolyFilled(points, static_cast<int>(std::size(points)), color);
}

// A coffee cup with a heart (the Ko-fi motif) `size` pixels square at `origin`.
void drawKofiCup(ImDrawList* drawList, ImVec2 origin, float size, ImU32 cupColor, ImU32 heartColor) {
    const auto at = [&](float x, float y) { return ImVec2(origin.x + x * size, origin.y + y * size); };
    // Fills the box like the GitHub mark: body 0.14-0.90 high, handle reaching the right edge.
    drawList->AddCircle(at(0.76f, 0.47f), 0.17f * size, cupColor, 0, 0.11f * size);  // handle
    drawList->AddRectFilled(at(0.02f, 0.14f), at(0.74f, 0.9f), cupColor, 0.16f * size, ImDrawFlags_RoundCornersBottom);
    // Heart: two lobes and a point, centred on the body.
    const float r = 0.105f * size;
    drawList->AddCircleFilled(at(0.285f, 0.43f), r, heartColor);
    drawList->AddCircleFilled(at(0.475f, 0.43f), r, heartColor);
    drawList->AddTriangleFilled(at(0.185f, 0.475f), at(0.575f, 0.475f), at(0.38f, 0.72f), heartColor);
}

void updateCheckTooltip() {
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::SetTooltip("%s", tr("Once a day, asks GitHub which release is the latest and shows a notice if it is "
                                   "newer.\nNothing is downloaded or installed automatically."));
    }
}

// Greyed-out help text that wraps at the column/window edge instead of running past it.
void hint(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

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
    return id.empty() ? tr("(none)") : tr("(not connected)");
}

// Volume 0-100 for a gain in dB: 100 is unity (0 dB) and 0 is silence, along the square of
// the amplitude (50 = -12 dB), close to how loudness is heard. A boost set with a Mixer fader
// shows above 100.
float dbToVolume(float db) { return db <= kFaderMinDb ? 0.0f : 100.0f * std::pow(10.0f, db / 40.0f); }

float volumeToDb(float volume) {
    if (volume < 0.5f) return kFaderMinDb;
    return std::clamp(40.0f * std::log10(volume / 100.0f), kFaderMinDb, kFaderMaxDb);
}

// Small 0-100 drag box for a channel's volume (double-click to type); red while muted.
bool volumeDrag(const char* id, ChannelStrip& strip) {
    float volume = dbToVolume(strip.gainDb);
    const float before = volume;
    if (strip.mute) ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.45f, 0.10f, 0.14f, 1));
    ImGui::DragFloat(id, &volume, 0.4f, 0.0f, 100.0f, "%.0f");
    if (strip.mute) ImGui::PopStyleColor();
    if (volume == before) return false;
    strip.gainDb = volumeToDb(volume);
    return true;
}

bool volumeSlider(const char* label, float& gainDb) {
    float volume = dbToVolume(gainDb);
    if (!ImGui::SliderFloat(label, &volume, 0.0f, 100.0f, "%.0f")) return false;
    gainDb = volumeToDb(volume);
    return true;
}

// Matrix cells: a checkerboard of device groups plus every other row a shade lighter, so a
// cell is easy to place; `lit` for the row and column under the mouse.
ImU32 cellBackground(std::size_t inputGroup, std::size_t outputGroup, std::uint32_t row, bool lit) {
    const int alpha = ((inputGroup + outputGroup) % 2 == 1 ? 16 : 0) + (row % 2 == 1 ? 7 : 0);
    return lit ? IM_COL32(110, 160, 255, alpha + 34) : IM_COL32(255, 255, 255, alpha);
}

void nameTooltip(const std::string& name, const std::string& label) {
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) return;
    if (label != name) ImGui::SetTooltip(tr("%s (original name: %s)\nRight-click to rename."), label.c_str(), name.c_str());
    else ImGui::SetTooltip(tr("%s\nRight-click to rename."), name.c_str());
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

// Status row under the title bar: the logo at full size on the left, the controls aligned to
// the right edge. When they do not fit on one line beside the logo, they fill the space to
// its right and wrap onto further lines.
void App::drawToolbar() {
    const float s = scale();
    const float frameH = ImGui::GetFrameHeight();
    const float logoH = frameH * 1.6f;
    const float logoW = brand::wordmarkWidth(logoH);
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float edge = start.x + ImGui::GetContentRegionAvail().x;

    brand::wordmark(logoH);
    const float logoBottom = start.y + logoH;
    const float right = edge;
    const float dotW = ImGui::GetFontSize() * 0.75f + ImGui::GetStyle().ItemSpacing.x;
    // SameLine if the next item (of width w) still fits after `spacing`, else a new line.
    const auto next = [&](float w, float spacing) {
        if (ImGui::GetItemRectMax().x + spacing + w <= right) ImGui::SameLine(0, spacing);
    };

    char status[256];
    if (running_) {
        std::snprintf(status, sizeof(status), tr("%s  |  %.0f Hz  |  %u samples (%.2f ms)"), host_.driverName().c_str(),
                      host_.sampleRate(), host_.blockFrames(), host_.blockFrames() * 1000.0 / host_.sampleRate());
    } else {
        std::snprintf(status, sizeof(status), tr("%s  (stopped)"),
                      settings_.driverName.empty() ? tr("No driver selected") : settings_.driverName.c_str());
    }
    const bool client = vasio_.clientConnected();
    char clientText[96];
    if (client) {
        std::snprintf(clientText, sizeof(clientText), tr("ASIO client connected (PID %u)"), vasio_.clientPid());
    } else {
        std::snprintf(clientText, sizeof(clientText), "%s",
                      driverStatus_ != driver::Status::NotRegistered ? tr("ASIO client: waiting for Traktor/DAW")
                                                                     : tr("Virtual ASIO driver not registered"));
    }

    // Right-align the controls when they fit on one line beside the logo; otherwise start
    // right after the logo and let them wrap.
    float total = 80 * s + 16 * s + dotW + ImGui::CalcTextSize(status).x + 24 * s + 110 * s + 24 * s + dotW +
                  ImGui::CalcTextSize(clientText).x;
    if (settings_.pttEnabled) total += 24 * s + 190 * s;
    const float controlsLeft = start.x + logoW + 24 * s;
    ImGui::SetCursorScreenPos(ImVec2(std::max(controlsLeft, edge - total), start.y + (logoH - frameH) * 0.5f));
    ImGui::BeginGroup();

    if (running_) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.18f, 0.16f, 1));
        if (ImGui::Button(tr("Stop###start-stop"), ImVec2(80 * s, 0))) stopAudio();
        ImGui::PopStyleColor();
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.45f, 0.25f, 1));
        if (ImGui::Button(tr("Start###start-stop"), ImVec2(80 * s, 0))) startAudio();
        ImGui::PopStyleColor();
    }

    next(dotW + ImGui::CalcTextSize(status).x, 16 * s);
    ImGui::BeginGroup();
    statusDot(running_, true);
    ImGui::AlignTextToFramePadding();
    if (running_) ImGui::TextUnformatted(status);
    else ImGui::TextDisabled("%s", status);
    ImGui::EndGroup();

    next(110 * s, 24 * s);
    char load[32];
    std::snprintf(load, sizeof(load), "DSP %.0f%%", dspLoad_ * 100.0f);
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, dspPeak_ > 0.8f ? ImVec4(0.85f, 0.3f, 0.25f, 1) : ImGui::GetStyle().Colors[ImGuiCol_PlotHistogram]);
    ImGui::ProgressBar(std::clamp(dspLoad_, 0.0f, 1.0f), ImVec2(110 * s, 0), load);
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(tr("Engine time per block / block period.\nPeak: %.0f%%   Driver overloads: %llu"), dspPeak_ * 100.0f,
                          static_cast<unsigned long long>(host_.overloadCount()));
    }

    next(dotW + ImGui::CalcTextSize(clientText).x, 24 * s);
    ImGui::BeginGroup();
    statusDot(client, running_);
    ImGui::AlignTextToFramePadding();
    if (client) ImGui::TextUnformatted(clientText);
    else ImGui::TextDisabled("%s", clientText);
    ImGui::EndGroup();

    if (settings_.pttEnabled) {
        next(190 * s, 24 * s);
        drawPttButton();
    }
    ImGui::EndGroup();
    // Continue below whichever is taller: the controls or the logo.
    if (ImGui::GetCursorScreenPos().y < logoBottom) ImGui::Dummy(ImVec2(0, logoBottom - ImGui::GetCursorScreenPos().y));
    ImGui::Spacing();
}

// ---- Matrix -------------------------------------------------------------------------------

void App::drawMatrix() {
    const std::uint32_t nIn = routing_.numInputs();
    const std::uint32_t nOut = routing_.numOutputs();
    if (nIn == 0 || nOut == 0) {
        ImGui::TextDisabled("%s", tr("Start the audio engine to see the routing matrix."));
        return;
    }
    ImGui::TextDisabled("%s", tr("Click a cell to route or unroute; right-click it for volume, mute and pan. Right-click a "
                                 "name to rename it. Volumes go from 0 to 100: drag them, or double-click to type."));

    const float s = scale();
    // Columns share the window width: wider cells on big windows, a scrollbar only when even
    // the minimum width does not fit. The name column also holds the input volume and meter.
    const ImGuiStyle& style = ImGui::GetStyle();
    const float avail = ImGui::GetContentRegionAvail().x - style.ScrollbarSize;
    const float perColumn = 2.0f * style.CellPadding.x + 1.0f;  // padding + inner border
    const float volumeWidth = 40 * s;
    const float meterWidth = 46 * s;
    const float nameWidth = std::clamp(avail * 0.22f, 220 * s, 330 * s);
    const float labelWidth = nameWidth - volumeWidth - meterWidth - 2 * style.ItemSpacing.x;
    const float cellWidth = std::clamp((avail - nameWidth - perColumn * static_cast<float>(nOut + 1)) / static_cast<float>(nOut),
                                       28 * s, 72 * s);

    const ImGuiTableFlags flags = ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV |
                                  ImGuiTableFlags_BordersOuter | ImGuiTableFlags_SizingFixedFit;
    if (!ImGui::BeginTable("matrix", static_cast<int>(nOut) + 1, flags)) return;

    ImGui::TableSetupColumn(tr("Inputs  \\  Outputs###io"), ImGuiTableColumnFlags_NoHide | ImGuiTableColumnFlags_WidthFixed, nameWidth);
    for (std::uint32_t o = 0; o < nOut; ++o) {
        ImGui::TableSetupColumn(outputLabel(o).c_str(), ImGuiTableColumnFlags_AngledHeader | ImGuiTableColumnFlags_WidthFixed,
                                cellWidth);
    }
    ImGui::TableSetupScrollFreeze(1, 2);
    ImGui::TableAngledHeadersRow();
    ImGui::TableHeadersRow();

    // The row and column under the mouse (from the previous frame) are lit like a crosshair.
    const int crossRow = hoveredRow_;
    const int crossColumn = hoveredColumn_;
    hoveredRow_ = hoveredColumn_ = -1;

    for (std::uint32_t i = 0; i < nIn; ++i) {
        ImGui::TableNextRow();
        const std::size_t ig = groupOf(inputGroups_, i);
        const bool rowLit = static_cast<int>(i) == crossRow;
        // Rows and the output-volume row get separate ID scopes: input i and output i must
        // never share a popup or a text field.
        ImGui::PushID("in");
        ImGui::PushID(static_cast<int>(i));

        // Name (right-click: rename), volume, meter.
        ImGui::TableSetColumnIndex(0);
        ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, withAlpha(groupColor(ig), rowLit ? 70 : 28));
        ImGui::PushStyleColor(ImGuiCol_Text, groupColor(ig));
        ImGui::AlignTextToFramePadding();
        ImGui::Selectable(inputLabel(i).c_str(), false, ImGuiSelectableFlags_None, ImVec2(labelWidth, 0));
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) hoveredRow_ = static_cast<int>(i);
        nameTooltip(routing_.inputName(i), inputLabel(i));
        if (ImGui::BeginPopupContextItem("rename")) {
            drawRenameField(true, routing_.inputName(i));
            ImGui::EndPopup();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(volumeWidth);
        routingDirty_ |= volumeDrag("##volume", routing_.input(i));
        if (ImGui::IsItemHovered()) hoveredRow_ = static_cast<int>(i);
        ImGui::SameLine();
        horizontalMeter(inputMeters_[i], meterWidth, ImGui::GetTextLineHeight() * 0.6f);

        for (std::uint32_t o = 0; o < nOut; ++o) {
            ImGui::TableSetColumnIndex(static_cast<int>(o) + 1);
            const std::size_t og = groupOf(outputGroups_, o);
            ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg,
                                   cellBackground(ig, og, i, rowLit || static_cast<int>(o) == crossColumn));
            Crosspoint& xp = routing_.at(o, i);
            char label[24];
            if (!xp.routed) std::snprintf(label, sizeof(label), "###c");
            else if (xp.mute) std::snprintf(label, sizeof(label), "M###c");
            else std::snprintf(label, sizeof(label), "%.0f###c", dbToVolume(xp.gainDb));

            // Active routes wear the logo gradient; the button on top is translucent so the label stays readable.
            const bool live = xp.routed && !xp.mute;
            if (live) {
                const ImVec2 p0 = ImGui::GetCursorScreenPos();
                brand::gradientRect(ImGui::GetWindowDrawList(), p0,
                                    ImVec2(p0.x + ImGui::GetContentRegionAvail().x, p0.y + ImGui::GetFrameHeight()),
                                    ImGui::GetStyle().FrameRounding);
            }
            // Empty cells are translucent so the grid pattern shows through.
            const ImVec4 bg = live      ? ImVec4(0, 0, 0, 0)
                              : xp.mute ? ImVec4(0.45f, 0.10f, 0.14f, 1)
                                        : ImVec4(0.07f, 0.09f, 0.13f, 0.55f);
            ImGui::PushID(static_cast<int>(o));
            ImGui::PushStyleColor(ImGuiCol_Button, bg);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, live ? ImVec4(1, 1, 1, 0.18f) : ImVec4(bg.x + 0.1f, bg.y + 0.1f, bg.z + 0.12f, 1));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, live ? ImVec4(1, 1, 1, 0.3f) : ImVec4(bg.x + 0.18f, bg.y + 0.18f, bg.z + 0.2f, 1));
            if (ImGui::Button(label, ImVec2(-FLT_MIN, 0))) {
                xp.routed = !xp.routed;
                routingDirty_ = true;
            }
            ImGui::PopStyleColor(3);
            if (ImGui::IsItemHovered()) {
                hoveredRow_ = static_cast<int>(i);
                hoveredColumn_ = static_cast<int>(o);
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                ImGui::SetTooltip(tr("%s  ->  %s\n%s, volume %.0f%s"), inputLabel(i).c_str(), outputLabel(o).c_str(),
                                  xp.routed ? tr("routed") : tr("not routed"), dbToVolume(xp.gainDb),
                                  xp.mute ? tr(", muted") : "");
            }
            if (ImGui::BeginPopupContextItem("xp")) {
                ImGui::Text("%s  ->  %s", inputLabel(i).c_str(), outputLabel(o).c_str());
                ImGui::Separator();
                bool changed = ImGui::Checkbox(tr("Routed"), &xp.routed);
                ImGui::SetNextItemWidth(220 * s);
                changed |= volumeSlider(tr("Volume"), xp.gainDb);
                changed |= ImGui::Checkbox(tr("Mute"), &xp.mute);
                if (routing_.outputSide(o) != PanSide::Mono) {
                    ImGui::SetNextItemWidth(220 * s);
                    changed |= ImGui::SliderFloat(tr("Pan"), &xp.pan, -1.0f, 1.0f, "%.2f");
                }
                if (ImGui::Button(tr("Reset"))) {
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
        ImGui::PopID();
        ImGui::PopID();
    }

    // Last row: each output's volume (right-click: rename the output).
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", tr("Output volume"));
    for (std::uint32_t o = 0; o < nOut; ++o) {
        ImGui::TableSetColumnIndex(static_cast<int>(o) + 1);
        if (static_cast<int>(o) == crossColumn) ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, IM_COL32(110, 160, 255, 34));
        ImGui::PushID("out");
        ImGui::PushID(static_cast<int>(o));
        ImGui::SetNextItemWidth(-FLT_MIN);
        routingDirty_ |= volumeDrag("##out-volume", routing_.output(o));
        if (ImGui::IsItemHovered()) hoveredColumn_ = static_cast<int>(o);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
            ImGui::SetTooltip(tr("%s: volume %.0f\nRight-click to rename."), outputLabel(o).c_str(),
                              dbToVolume(routing_.output(o).gainDb));
        }
        if (ImGui::BeginPopupContextItem("rename")) {
            drawRenameField(false, routing_.outputName(o));
            ImGui::EndPopup();
        }
        ImGui::PopID();
        ImGui::PopID();
    }
    ImGui::EndTable();
}

// ---- Channel names ------------------------------------------------------------------------

const std::string& App::inputLabel(std::uint32_t i) const {
    const auto it = settings_.inputAliases.find(routing_.inputName(i));
    return it != settings_.inputAliases.end() ? it->second : routing_.inputName(i);
}

const std::string& App::outputLabel(std::uint32_t o) const {
    const auto it = settings_.outputAliases.find(routing_.outputName(o));
    return it != settings_.outputAliases.end() ? it->second : routing_.outputName(o);
}

// Inside a popup: the display name of channel `name`. Only the label changes; routes, MIDI
// mappings and settings keep using the channel's own name.
void App::drawRenameField(bool input, const std::string& name) {
    auto& aliases = input ? settings_.inputAliases : settings_.outputAliases;
    const auto current = aliases.find(name);
    ImGui::TextDisabled(tr("Original name: %s"), name.c_str());
    char text[64] = {};
    std::snprintf(text, sizeof(text), "%s", current != aliases.end() ? current->second.c_str() : name.c_str());
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    ImGui::SetNextItemWidth(240 * scale());
    if (ImGui::InputText(tr("Name"), text, sizeof(text), ImGuiInputTextFlags_AutoSelectAll)) {
        std::string alias = text;
        alias.erase(0, alias.find_first_not_of(' '));
        alias.erase(alias.find_last_not_of(' ') + 1);
        if (alias.empty() || alias == name) aliases.erase(name);
        else aliases[name] = alias;
        settingsDirty_ = true;
    }
    if (ImGui::IsItemDeactivated() && ImGui::IsKeyPressed(ImGuiKey_Enter)) ImGui::CloseCurrentPopup();
    if (aliases.count(name) != 0 && ImGui::SmallButton(tr("Use the original name"))) {
        aliases.erase(name);
        settingsDirty_ = true;
    }
}

// ---- Mixer --------------------------------------------------------------------------------

bool App::drawStrip(const char* id, bool input, std::uint32_t ch, unsigned color, float width) {
    const float s = scale();
    const std::string& name = input ? routing_.inputName(ch) : routing_.outputName(ch);
    const std::string& label = input ? inputLabel(ch) : outputLabel(ch);
    ChannelStrip& strip = input ? routing_.input(ch) : routing_.output(ch);
    const Meter& meter = input ? inputMeters_[ch] : outputMeters_[ch];
    // USB mics are often quiet: their input strips reach further up than the rest.
    const bool isMic = input && std::any_of(inputGroups_.begin(), inputGroups_.end(), [ch](const ChannelGroup& g) {
        return g.name == "Mic" && ch >= g.begin && ch < g.begin + g.count;
    });
    const float maxDb = isMic ? kMicFaderMaxDb : kFaderMaxDb;
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
    dl->AddText(ImVec2(p.x, p.y + 5 * s), IM_COL32(220, 220, 225, 255), label.c_str());
    dl->PopClipRect();
    ImGui::InvisibleButton("##name", ImVec2(width, lineH + 6 * s));
    nameTooltip(name, label);
    if (ImGui::BeginPopupContextItem("rename")) {
        drawRenameField(input, name);
        ImGui::EndPopup();
    }

    changed |= ImGui::VSliderFloat("##fader", ImVec2(width * 0.55f, height), &strip.gainDb, kFaderMinDb, maxDb, "");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Double-click: 0 dB"));
    // The first click of a double-click already moved the fader. On the second, reset it and
    // release the slider, or the still-pressed button would drag it away from 0 dB again.
    if (ImGui::IsItemActive() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        strip.gainDb = 0.0f;
        changed = true;
        ImGui::ClearActiveID();
    }
    ImGui::SameLine(0, 4 * s);
    drawMeter(meter, ImVec2(width * 0.45f - 4 * s, height));

    // Gain in dB: drag to adjust, or double-click (or Ctrl+click) and type a value.
    ImGui::SetNextItemWidth(width);
    changed |= ImGui::DragFloat("##db", &strip.gainDb, 0.1f, kFaderMinDb, maxDb, "%+.1f dB", ImGuiSliderFlags_AlwaysClamp);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::SetTooltip(tr("Drag to adjust, or double-click and type the gain in dB (-60 = off, up to %+.0f)."), maxDb);
    }

    ImGui::PushStyleColor(ImGuiCol_Button, strip.mute ? ImVec4(0.75f, 0.2f, 0.18f, 1) : ImGui::GetStyle().Colors[ImGuiCol_Button]);
    if (ImGui::Button(tr("MUTE"), ImVec2(width, 0))) {
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
        ImGui::TextDisabled("%s", tr("Start the audio engine to see the mixer."));
        return;
    }
    const float s = scale();
    const float half = (ImGui::GetContentRegionAvail().y - ImGui::GetStyle().ItemSpacing.y) * 0.5f;

    auto section = [&](const char* id, const char* title, const std::vector<ChannelGroup>& groups, bool inputs) {
        if (ImGui::BeginChild(id, ImVec2(0, half), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar)) {
            ImGui::TextDisabled("%s", title);
            // Strips share the width: wider on big windows, scroll only below the minimum.
            std::uint32_t strips = 0;
            for (const ChannelGroup& group : groups) strips += group.count;
            const float gaps = 6 * s * static_cast<float>(strips - std::min<std::uint32_t>(strips, static_cast<std::uint32_t>(groups.size()))) +
                               18 * s * static_cast<float>(groups.empty() ? 0 : groups.size() - 1);
            const float stripWidth = strips == 0 ? 66 * s
                                                 : std::clamp((ImGui::GetContentRegionAvail().x - gaps) / static_cast<float>(strips),
                                                              56 * s, 120 * s);
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
                    routingDirty_ |= drawStrip(stripId, inputs, ch, groupColor(g), stripWidth);
                }
                ImGui::EndGroup();
            }
        }
        ImGui::EndChild();
    };
    section("inputs", tr("INPUTS"), inputGroups_, true);
    section("outputs", tr("OUTPUT BUSES"), outputGroups_, false);
}

// ---- Devices ------------------------------------------------------------------------------

void App::drawDevices() {
    const float s = scale();
    // Wide windows get two columns: the master interface and Traktor's driver on the left,
    // mic, stream and push-to-talk on the right. Combos take a share of their column.
    const float avail = ImGui::GetContentRegionAvail().x;
    const bool twoColumns = avail >= 1050 * s;
    const float columnWidth = twoColumns ? avail * 0.5f : avail;
    const float comboWidth = std::clamp(columnWidth * 0.6f, 220 * s, 560 * s);
    bool changed = false;

    ImGui::SeparatorText(tr("General"));
    changed |= ImGui::Checkbox(tr("Start audio when AudioFlow Matrix opens"), &settings_.autoStart);
    ImGui::Spacing();

    if (twoColumns) {
        ImGui::BeginTable("devices-columns", 2, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_BordersInnerV);
        ImGui::TableNextColumn();
    }

    ImGui::SeparatorText(tr("Master ASIO interface (clock source)"));
    ImGui::SetNextItemWidth(comboWidth);
    if (ImGui::BeginCombo(tr("Driver"), settings_.driverName.empty() ? tr("(none)") : settings_.driverName.c_str())) {
        for (const auto& d : drivers_) {
            if (ImGui::Selectable(d.name.c_str(), d.name == settings_.driverName)) {
                settings_.driverName = d.name;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    if (drivers_.empty()) ImGui::TextColored(ImVec4(0.95f, 0.65f, 0.2f, 1), "%s", tr("No ASIO drivers found. Install your interface's ASIO driver."));

    // 0 = leave the interface at whatever rate it is already running at.
    static const double kRates[] = {0.0, 44100.0, 48000.0, 88200.0, 96000.0};
    const auto rateName = [this](double r, char* buf, std::size_t size) {
        if (r > 0.0) std::snprintf(buf, size, "%.0f Hz", r);
        else if (running_) std::snprintf(buf, size, tr("Driver setting (%.0f Hz)"), host_.sampleRate());
        else std::snprintf(buf, size, "%s", tr("Driver setting"));
    };
    char rateLabel[48];
    rateName(settings_.sampleRate, rateLabel, sizeof(rateLabel));
    ImGui::SetNextItemWidth(comboWidth);
    if (ImGui::BeginCombo(tr("Sample rate"), rateLabel)) {
        for (double r : kRates) {
            char item[48];
            rateName(r, item, sizeof(item));
            if (ImGui::Selectable(item, r == settings_.sampleRate)) {
                settings_.sampleRate = r;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::BeginDisabled(!running_);
    if (ImGui::Button(tr("Open driver control panel"))) host_.openControlPanel();
    ImGui::EndDisabled();
    ImGui::SameLine();
    hint(tr("Buffer size is set in the interface's own control panel."));

    ImGui::SeparatorText(tr("Virtual ASIO driver for Traktor / DAWs"));
    if (driverStatus_ == driver::Status::Registered) {
        ImGui::TextColored(ImVec4(0.3f, 0.85f, 0.4f, 1), "%s", tr("\"AudioFlow Matrix ASIO\" is registered."));
        ImGui::SameLine();
        if (ImGui::SmallButton(tr("Unregister"))) registerVirtualDriver(false);
    } else if (driverStatus_ == driver::Status::RegisteredOther) {
        // Another build's driver (an older version, or one registered from a user folder).
        const std::wstring other = driver::registeredPath();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.65f, 0.2f, 1));
        ImGui::TextWrapped(tr("The registered driver belongs to another version of AudioFlow Matrix: %s. Update it so "
                              "Traktor uses this version's driver."),
                           other.empty() ? tr("(file missing)") : win::toUtf8(other).c_str());
        ImGui::PopStyleColor();
        if (ImGui::Button(tr("Update driver (administrator)"))) registerVirtualDriver(true);
        ImGui::SameLine();
        if (ImGui::SmallButton(tr("Unregister"))) registerVirtualDriver(false);
    } else {
        ImGui::TextColored(ImVec4(0.95f, 0.65f, 0.2f, 1), "%s", tr("Not registered."));
        ImGui::SameLine();
        if (ImGui::Button(tr("Register driver (administrator)"))) registerVirtualDriver(true);
    }
    hint(tr("In Traktor: Preferences > Audio Setup > Audio Device: \"AudioFlow Matrix ASIO\". Start AudioFlow Matrix "
            "first."));
    if (vasio_.clientConnected()) {
        // Late blocks are gaps in the client's audio (clicks); the reserve grows by itself
        // when they happen. "Recent" is the last 10 seconds.
        const std::uint32_t safety = vasio_.safetyBlocks();
        const double safetyMs = running_ ? safety * host_.blockFrames() * 1000.0 / host_.sampleRate() : 0.0;
        const std::uint64_t late = vasio_.lateBlocks();
        const std::uint64_t recent = late - clientLateHistory_.front();
        ImGui::Text(tr("Client PID %u  |  safety buffer %u blocks (%.1f ms)  |  late blocks: %llu in the last 10 s, "
                       "%llu in total"),
                    vasio_.clientPid(), safety, safetyMs, static_cast<unsigned long long>(recent),
                    static_cast<unsigned long long>(late));
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
            ImGui::SetTooltip("%s", tr("A late block is a short gap (a click) in the audio coming from Traktor. When "
                                       "one happens, the safety buffer grows by one block, adding a little latency, "
                                       "so it does not happen again."));
        }
        if (vasio_.clientStalled()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.65f, 0.2f, 1));
            ImGui::TextWrapped("%s", tr("The client stopped sending audio. Restart its audio (in Traktor: Preferences "
                                        "> Audio Setup, choose the device again)."));
            ImGui::PopStyleColor();
        }
    }

    if (twoColumns) ImGui::TableNextColumn();
    ImGui::SeparatorText(tr("USB microphone (WASAPI)"));
    changed |= ImGui::Checkbox(tr("Enable##mic"), &settings_.micEnabled);
    ImGui::SameLine();
    changed |= ImGui::Checkbox(tr("Exclusive mode##mic"), &settings_.micExclusive);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::SetTooltip("%s", tr("Exclusive: slightly lower latency, but Windows then gives the mic to AudioFlow Matrix "
                                   "only.\nGames, Discord or the browser can no longer use it. Leave it off unless nothing "
                                   "else needs the mic."));
    }
    if (settings_.micExclusive) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.65f, 0.2f, 1));
        ImGui::TextWrapped("%s", tr("Exclusive mode: other apps (games, Discord, the browser) cannot use this mic while "
                                    "AudioFlow Matrix is running."));
        ImGui::PopStyleColor();
    }
    ImGui::SetNextItemWidth(comboWidth);
    changed |= deviceCombo(tr("Capture device"), captureDevices_, settings_.micId);
    if (mic_) {
        const auto& info = mic_->info();
        ImGui::Text(tr("%s  |  drift %+.1f ppm  |  %s  |  dropouts %llu"), info.format.c_str(), mic_->resampler().correctionPpm(),
                    mic_->resampler().locked() ? tr("locked") : tr("priming"),
                    static_cast<unsigned long long>(mic_->resampler().dropouts()));
        if (mic_->deviceLost()) ImGui::TextColored(ImVec4(0.95f, 0.4f, 0.3f, 1), "%s", tr("Device disconnected. Press Apply to reopen."));
    }
    changed |= drawVoiceControls();

    ImGui::SeparatorText(tr("Windows sound loopback (WASAPI)"));
    changed |= ImGui::Checkbox(tr("Enable##loopback"), &settings_.loopbackEnabled);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::SetTooltip("%s", tr("Adds an input with whatever Windows plays on the chosen device (games, Discord, the "
                                   "browser).\nOther apps keep playing there as usual."));
    }
    ImGui::SetNextItemWidth(comboWidth);
    changed |= deviceCombo(tr("Playback device##loopback"), renderDevices_, settings_.loopbackId);
    if (settings_.loopbackEnabled && settings_.streamEnabled && !settings_.loopbackId.empty() &&
        settings_.loopbackId == settings_.streamId) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.65f, 0.2f, 1));
        ImGui::TextWrapped("%s", tr("This is also the stream output device: routing Loopback to Stream would feed the "
                                    "sound back into itself."));
        ImGui::PopStyleColor();
    }
    if (loopback_) {
        const auto& info = loopback_->info();
        ImGui::Text(tr("%s  |  drift %+.1f ppm  |  %s  |  dropouts %llu"), info.format.c_str(),
                    loopback_->resampler().correctionPpm(), loopback_->resampler().locked() ? tr("locked") : tr("priming"),
                    static_cast<unsigned long long>(loopback_->resampler().dropouts()));
        if (loopback_->deviceLost()) ImGui::TextColored(ImVec4(0.95f, 0.4f, 0.3f, 1), "%s", tr("Device disconnected. Press Apply to reopen."));
    }

    ImGui::SeparatorText(tr("Stream output for Discord / OBS (WASAPI)"));
    changed |= ImGui::Checkbox(tr("Enable##stream"), &settings_.streamEnabled);
    ImGui::SameLine();
    changed |= ImGui::Checkbox(tr("Exclusive mode##stream"), &settings_.streamExclusive);
    ImGui::SetNextItemWidth(comboWidth);
    changed |= deviceCombo(tr("Render device"), renderDevices_, settings_.streamId);
    hint(tr("Tip: with a virtual cable (e.g. VB-CABLE) pick \"CABLE Input\" here and \"CABLE Output\" as the "
            "microphone in Discord / OBS."));
    if (stream_) {
        const auto& info = stream_->info();
        ImGui::Text(tr("%s  |  drift %+.1f ppm  |  %s  |  dropouts %llu"), info.format.c_str(), stream_->resampler().correctionPpm(),
                    stream_->resampler().locked() ? tr("locked") : tr("priming"),
                    static_cast<unsigned long long>(stream_->resampler().dropouts()));
        if (stream_->deviceLost()) ImGui::TextColored(ImVec4(0.95f, 0.4f, 0.3f, 1), "%s", tr("Device disconnected. Press Apply to reopen."));
    }

    changed |= drawPttSettings();
    if (twoColumns) ImGui::EndTable();

    ImGui::Separator();
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.45f, 0.25f, 1));
    const char* applyLabel = tr("Apply and restart audio");
    const float applyWidth = ImGui::CalcTextSize(applyLabel).x + 2 * ImGui::GetStyle().FramePadding.x;
    if (ImGui::Button(applyLabel, ImVec2(std::max(220 * s, applyWidth), 0))) {
        saveSettings();
        startAudio();
    }
    ImGui::PopStyleColor();
    ImGui::SameLine();
    if (ImGui::Button(tr("Refresh device lists"))) {
        refreshDevices();
        if (!midiIn_.isOpen()) openMidi();  // a MIDI controller plugged in after start-up
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("Clear all routes"))) {
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
    if (ImGui::Checkbox(tr("Denoise (RNNoise)"), &settings_.micDenoise)) {
        changed = true;
        if (voice) voice->setDenoise(settings_.micDenoise);
    }
    ImGui::EndDisabled();
    ImGui::SameLine(0, 20 * s);
    if (ImGui::Checkbox(tr("Gate"), &settings_.micGate)) {
        changed = true;
        if (voice) voice->setGate(settings_.micGate);
    }
    ImGui::SameLine(0, 20 * s);
    ImGui::SetNextItemWidth(220 * s);
    if (ImGui::SliderFloat(tr("Threshold"), &settings_.micGateThresholdDb, dsp::NoiseGate::kMinThresholdDb,
                           dsp::NoiseGate::kMaxThresholdDb, "%.0f dB")) {
        changed = true;
        if (voice) voice->setGateThresholdDb(settings_.micGateThresholdDb);
    }

    if (voice == nullptr) return changed;
    if (!denoiseAvailable) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.65f, 0.2f, 1));
        ImGui::TextWrapped(tr("Denoise needs the mic at 48000 Hz; it runs at %.0f Hz. Set 48000 Hz in Windows (Sound "
                              "settings > this mic > Properties > Advanced) and press Apply and restart audio."),
                           mic_->info().deviceRate);
        ImGui::PopStyleColor();
    }
    statusDot(voice->gateOpen() || !settings_.micGate, true);
    ImGui::AlignTextToFramePadding();
    if (settings_.micGate) {
        const float gain = voice->gateGain();
        const float gainDb = gain > 1e-3f ? 20.0f * std::log10(gain) : -60.0f;
        ImGui::Text(tr("Gate %s (%.0f dB)"), voice->gateOpen() ? tr("open") : tr("closed"), gainDb);
    } else {
        ImGui::TextDisabled("%s", tr("Gate off"));
    }
    if (denoiseAvailable) {
        ImGui::SameLine(0, 24 * s);
        ImGui::Text(tr("Voice %3.0f%%"), voice->voiceProbability() * 100.0f);
        ImGui::SameLine(0, 24 * s);
        ImGui::TextDisabled(tr("+%.0f ms latency"), voice->latencyFrames() * 1000.0 / mic_->info().deviceRate);
    }
    return changed;
}

// ---- Status bar ---------------------------------------------------------------------------

void App::drawStatusBar() {
    ImGui::Separator();
    const float s = scale();
    const ImGuiStyle& style = ImGui::GetStyle();
    // A compact line: the checkbox box is about as tall as the text, and every item is
    // centred on it through AlignTextToFramePadding.
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(style.FramePadding.x, 2 * s));
    const float right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
    ImGui::AlignTextToFramePadding();
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
        ImGui::TextDisabled(tr("%s  |  %u x %u matrix  |  mixer: %s"), running_ ? tr("Running") : tr("Stopped"), routing_.numInputs(),
                            routing_.numOutputs(), dsp::MatrixMixer::usingAvx2() ? "AVX2/FMA" : "scalar");
    }

    // Right corner: version, the update check toggle and a link to the project page.
    const char* versionText = "v" AFM_VERSION;
    const char* checkLabel = tr("Check for updates###update-check");
    const char* linkText = "GitHub";
    const float gap = 16 * s;
    const float lineHeight = ImGui::GetFrameHeight();
    const float iconSize = std::round(ImGui::GetFontSize());
    const ImVec2 linkTextSize = ImGui::CalcTextSize(linkText);
    const ImVec2 linkSize(iconSize + style.ItemInnerSpacing.x + linkTextSize.x, lineHeight);
    const char* kofiText = "Ko-fi";
    const ImVec2 kofiTextSize = ImGui::CalcTextSize(kofiText);
    const ImVec2 kofiSize(iconSize + style.ItemInnerSpacing.x + kofiTextSize.x, lineHeight);
    const float width = ImGui::CalcTextSize(versionText).x + gap + lineHeight + style.ItemInnerSpacing.x +
                        ImGui::CalcTextSize(checkLabel, nullptr, true).x + gap + linkSize.x + gap + kofiSize.x;
    const float leftEnd = ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x;
    const float rightPadding = 12 * s;  // keeps Ko-fi off the window's corner
    ImGui::SameLine(std::max(leftEnd + gap, right - width - rightPadding));
    ImGui::TextDisabled("%s", versionText);
    ImGui::SameLine(0, gap);
    drawUpdateCheckbox(checkLabel);
    updateCheckTooltip();

    // The mark and the word form one link, both centred on the line.
    ImGui::SameLine(0, gap);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    if (ImGui::InvisibleButton("github-link", linkSize)) {
        ShellExecuteW(nullptr, L"open", kProjectPage, nullptr, nullptr, SW_SHOWNORMAL);
    }
    const bool hovered = ImGui::IsItemHovered();
    const ImU32 color = ImGui::GetColorU32(hovered ? ImGuiCol_Text : ImGuiCol_TextLink);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawGithubMark(drawList, ImVec2(pos.x, std::round(pos.y + (lineHeight - iconSize) / 2)), iconSize, color);
    const ImVec2 textPos(pos.x + iconSize + style.ItemInnerSpacing.x, std::round(pos.y + (lineHeight - linkTextSize.y) / 2));
    drawList->AddText(textPos, color, linkText);
    if (hovered) {
        const float underline = textPos.y + linkTextSize.y;
        drawList->AddLine(ImVec2(textPos.x, underline), ImVec2(textPos.x + linkTextSize.x, underline), color);
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        ImGui::SetTooltip("%s", tr("Open the project page on GitHub"));
    }

    // Ko-fi: same style as the GitHub link, the cup keeps its red heart.
    ImGui::SameLine(0, gap);
    const ImVec2 kofiPos = ImGui::GetCursorScreenPos();
    if (ImGui::InvisibleButton("kofi-link", kofiSize)) {
        ShellExecuteW(nullptr, L"open", kKofiPage, nullptr, nullptr, SW_SHOWNORMAL);
    }
    const bool kofiHovered = ImGui::IsItemHovered();
    const ImU32 kofiColor = ImGui::GetColorU32(kofiHovered ? ImGuiCol_Text : ImGuiCol_TextLink);
    drawKofiCup(drawList, ImVec2(kofiPos.x, std::round(kofiPos.y + (lineHeight - iconSize) / 2)), iconSize, kofiColor,
                IM_COL32(255, 94, 91, 255));
    const ImVec2 kofiTextPos(kofiPos.x + iconSize + style.ItemInnerSpacing.x,
                             std::round(kofiPos.y + (lineHeight - kofiTextSize.y) / 2));
    drawList->AddText(kofiTextPos, kofiColor, kofiText);
    if (kofiHovered) {
        const float underline = kofiTextPos.y + kofiTextSize.y;
        drawList->AddLine(ImVec2(kofiTextPos.x, underline), ImVec2(kofiTextPos.x + kofiTextSize.x, underline), kofiColor);
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        ImGui::SetTooltip("%s", tr("Support AudioFlow Matrix on Ko-fi"));
    }
    ImGui::PopStyleVar();
}

} // namespace afm::app
