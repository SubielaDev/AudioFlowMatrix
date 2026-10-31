// "A new version is available" notice: picks up the background check's result and draws
// the banner under the toolbar. The check itself is in UpdateCheck.cpp.

#include "App.h"
#include "I18n.h"
#include "Version.h"

#include "imgui.h"

#include <shellapi.h>

#include <cstdio>
#include <ctime>

#ifndef AFM_VERSION
#define AFM_VERSION "0.0.0"
#endif

namespace afm::app {

void App::startUpdateCheck() {
    log_.write("Update check: asking GitHub for the latest release");
    updateCheck_.start();
    updatePolled_ = false;
    updateError_.clear();
}

void App::pollUpdateCheck() {
    if (updatePolled_) return;
    const UpdateCheck::State state = updateCheck_.state();
    if (state == UpdateCheck::State::Running || state == UpdateCheck::State::Idle) return;
    updatePolled_ = true;
    const std::string result = updateCheck_.result();
    if (state == UpdateCheck::State::Failed) {
        log_.write("Update check: %s", result.c_str());  // retried at the next start
        updateError_ = result;
        return;
    }
    settings_.updateLatest = result;
    settings_.updateLastCheck = static_cast<long long>(std::time(nullptr));
    settingsDirty_ = true;
    log_.write("Update check: latest release is %s, this is %s (%s)", result.c_str(), AFM_VERSION,
               version::isNewer(result, AFM_VERSION) ? "update available" : "up to date");
}

bool App::drawUpdateCheckbox(const char* label) {
    if (!ImGui::Checkbox(label, &settings_.updateCheck)) return false;
    settingsDirty_ = true;
    if (settings_.updateCheck) startUpdateCheck();  // now rather than at the next start
    return true;
}

// Settings tab: interface language and the update check.
void App::drawSettings() {
    const float s = ImGui::GetFontSize() / 16.0f;
    const auto hint = [](const char* text) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        ImGui::TextWrapped("%s", text);
        ImGui::PopStyleColor();
    };

    ImGui::SeparatorText(tr("Language"));
    const i18n::Language current = i18n::language();
    const char* currentName = "";
    for (const auto& l : i18n::languages()) {
        if (l.language == current) currentName = l.nativeName;
    }
    ImGui::SetNextItemWidth(260 * s);
    if (ImGui::BeginCombo("##language", currentName)) {
        for (const auto& l : i18n::languages()) {
            if (ImGui::Selectable(l.nativeName, l.language == current) && l.language != current) {
                i18n::setLanguage(l.language);
                settingsDirty_ = true;
                log_.write("Language: %s", l.code);
            }
        }
        ImGui::EndCombo();
    }
    hint(tr("Messages that come from Windows or the audio drivers, and the diagnostics log, stay in English."));

    ImGui::SeparatorText(tr("Window"));
    if (ImGui::Checkbox(tr("Minimize to the system tray"), &settings_.minimizeToTray)) {
        settingsDirty_ = true;
        log_.write("Minimize to tray: %s", settings_.minimizeToTray ? "on" : "off");
    }
    hint(tr("When minimized, AudioFlow Matrix leaves the taskbar and stays as an icon next to the clock, with audio "
            "and MIDI still running. Click the icon to open it again; right-click it to exit."));

    // Interface smoothness: 0 stands for the monitor's own refresh rate.
    char displayLabel[96];
    std::snprintf(displayLabel, sizeof(displayLabel), tr("Match the display (%u Hz)"), displayHz_ > 0 ? displayHz_ : 60u);
    const struct {
        int fps;
        const char* label;
    } options[] = {{15, tr("15 fps (lowest CPU use)")}, {30, tr("30 fps")}, {60, tr("60 fps (recommended)")},
                   {0, displayLabel}};
    const char* currentFps = options[2].label;
    for (const auto& o : options) {
        if (o.fps == settings_.uiFps) currentFps = o.label;
    }
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(tr("Interface smoothness"));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(260 * s);
    if (ImGui::BeginCombo("##ui-fps", currentFps)) {
        for (const auto& o : options) {
            if (ImGui::Selectable(o.label, o.fps == settings_.uiFps) && o.fps != settings_.uiFps) {
                settings_.uiFps = o.fps;
                settingsDirty_ = true;
                log_.write("Interface: %d fps (0 = display rate)", o.fps);
            }
        }
        ImGui::EndCombo();
    }
    hint(tr("Fewer frames per second use less CPU; the meters move less smoothly. Audio is not affected."));
    if (ImGui::Checkbox(tr("Slow down to 15 fps while the window is in the background"), &settings_.uiSlowInBackground)) {
        settingsDirty_ = true;
    }

    ImGui::SeparatorText(tr("Channel names"));
    const std::size_t renamed = settings_.inputAliases.size() + settings_.outputAliases.size();
    ImGui::BeginDisabled(renamed == 0);
    if (ImGui::Button(tr("Restore all channel names"))) {
        settings_.inputAliases.clear();
        settings_.outputAliases.clear();
        settingsDirty_ = true;
        log_.write("Channel names: all %zu restored", renamed);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled(tr("%zu renamed. To rename one, right-click its name in the Matrix or the Mixer."), renamed);

    ImGui::SeparatorText(tr("Updates"));
    drawUpdateCheckbox(tr("Check for updates###update-check-settings"));
    hint(tr("Once a day, asks GitHub which release is the latest and shows a notice if it is newer.\nNothing is "
            "downloaded or installed automatically."));
    const bool checking = updateCheck_.state() == UpdateCheck::State::Running;
    ImGui::BeginDisabled(checking);
    if (ImGui::Button(tr("Check now"))) {
        updateBannerHidden_ = false;
        startUpdateCheck();
    }
    ImGui::EndDisabled();
    ImGui::SameLine(0, 16 * s);
    ImGui::AlignTextToFramePadding();
    const std::string& latest = settings_.updateLatest;
    if (checking) {
        ImGui::TextDisabled("%s", tr("Checking..."));
    } else if (!updateError_.empty()) {
        ImGui::TextColored(ImVec4(0.95f, 0.65f, 0.2f, 1), tr("Could not check: %s"), updateError_.c_str());
    } else if (latest.empty()) {
        ImGui::TextDisabled("%s", tr("Not checked yet."));
    } else if (version::isNewer(latest, AFM_VERSION)) {
        ImGui::TextColored(ImVec4(0.3f, 0.65f, 1.0f, 1), tr("New version available: %s"), latest.c_str());
    } else {
        ImGui::TextColored(ImVec4(0.3f, 0.85f, 0.4f, 1), tr("Up to date (latest release: %s)."), latest.c_str());
    }
    ImGui::TextDisabled(tr("This version: %s"), AFM_VERSION);
}

void App::drawUpdateBanner() {
    const std::string& latest = settings_.updateLatest;
    if (!settings_.updateCheck || updateBannerHidden_ || latest.empty() || latest == settings_.updateIgnored ||
        !version::isNewer(latest, AFM_VERSION)) {
        return;
    }
    const float s = ImGui::GetFontSize() / 16.0f;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.10f, 0.20f, 0.36f, 1));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6 * s);
    const float height = ImGui::GetFrameHeight() + 2 * ImGui::GetStyle().WindowPadding.y;
    if (ImGui::BeginChild("update-banner", ImVec2(0, height), ImGuiChildFlags_AlwaysUseWindowPadding)) {
        ImGui::AlignTextToFramePadding();
        ImGui::Text(tr("A new version of AudioFlow Matrix is available: %s (you have %s)."), latest.c_str(), AFM_VERSION);
        ImGui::SameLine(0, 16 * s);
        if (ImGui::SmallButton(tr("Download"))) {
            // Opens the release page; the user downloads (and can check the SHA-256) there.
            ShellExecuteW(nullptr, L"open", UpdateCheck::releasePage(latest).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            log_.write("Update: opened the %s release page", latest.c_str());
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(tr("Ignore this version"))) {
            settings_.updateIgnored = latest;
            settingsDirty_ = true;
            log_.write("Update: %s ignored", latest.c_str());
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(tr("Later"))) updateBannerHidden_ = true;
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

} // namespace afm::app
