#include "PerfTest.h"

#include "WindowChrome.h"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <filesystem>

#ifndef AFM_VERSION_STRING
#define AFM_VERSION_STRING "dev"
#endif

namespace afm::app {

namespace {

constexpr double kWarmUpSeconds = 2.0;

LONGLONG ticks() {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t.QuadPart;
}

std::uint64_t processCycles() {
    ULONG64 cycles = 0;
    QueryProcessCycleTime(GetCurrentProcess(), &cycles);
    return cycles;
}

// Nominal CPU clock; process cycle counts are in these units.
double cpuMhz() {
    DWORD mhz = 0;
    DWORD size = sizeof(mhz);
    RegGetValueW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", L"~MHz", RRF_RT_REG_DWORD,
                 nullptr, &mhz, &size);
    return mhz;
}

} // namespace

const char* renderModeName(RenderMode mode) {
    switch (mode) {
    case RenderMode::VSync: return "vsync";
    case RenderMode::Cap60: return "cap60";
    case RenderMode::Display: return "display";
    }
    return "?";
}

PerfRecorder::PerfRecorder(RenderMode mode, bool movingMeters, double seconds, HWND window)
    : mode_(mode), moving_(movingMeters), seconds_(seconds), window_(window) {
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    frequency_ = f.QuadPart;
    start_ = ticks();
}

void PerfRecorder::reset(LONGLONG now) {
    start_ = secondStart_ = now;
    cycles0_ = processCycles();
    frames_ = secondFrames_ = 0;
    buildSum_ = buildMax_ = renderSum_ = renderMax_ = 0;
    perSecond_.clear();
}

void PerfRecorder::frame(double buildMs, double renderMs) {
    if (done_) return;
    const LONGLONG now = ticks();
    if (!warm_) {
        if (static_cast<double>(now - start_) / frequency_ < kWarmUpSeconds) return;
        warm_ = true;
        reset(now);
        return;
    }
    ++frames_;
    ++secondFrames_;
    buildSum_ += buildMs;
    buildMax_ = std::max(buildMax_, buildMs);
    renderSum_ += renderMs;
    renderMax_ = std::max(renderMax_, renderMs);
    if (now - secondStart_ >= frequency_) {
        perSecond_.push_back(std::to_string(secondFrames_));
        secondFrames_ = 0;
        secondStart_ = now;
    }
}

bool PerfRecorder::finished() {
    if (done_) return true;
    if (!warm_) return false;
    const double elapsed = static_cast<double>(ticks() - start_) / frequency_;
    if (elapsed < seconds_) return false;
    writeReport(elapsed);
    done_ = true;
    return true;
}

void PerfRecorder::writeReport(double seconds) {
    const double cpu = 100.0 * static_cast<double>(processCycles() - cycles0_) / (seconds * cpuMhz() * 1e6);
    RECT client{};
    GetClientRect(window_, &client);

    const wchar_t* appData = _wgetenv(L"APPDATA");
    const std::filesystem::path dir = std::filesystem::path(appData != nullptr ? appData : L".") / L"AudioFlowMatrix" / L"logs";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &now);
    char name[96];
    std::snprintf(name, sizeof(name), "perf-%s-%s-%04d%02d%02d-%02d%02d%02d.log", renderModeName(mode_),
                  moving_ ? "moving" : "silent", local.tm_year + 1900, local.tm_mon + 1, local.tm_mday, local.tm_hour,
                  local.tm_min, local.tm_sec);
    FILE* out = _wfopen((dir / name).c_str(), L"w");
    if (out == nullptr) return;

    const double frames = static_cast<double>(std::max<std::uint64_t>(frames_, 1));
    std::fprintf(out, "AudioFlow Matrix GUI performance test\n");
    std::fprintf(out, "version              %s\n", AFM_VERSION_STRING);
    std::fprintf(out, "render mode          %s\n", renderModeName(mode_));
    std::fprintf(out, "scene                %s\n", moving_ ? "moving meters (synthetic music)" : "silent (nothing changes)");
    std::fprintf(out, "monitor refresh      %u Hz\n", chrome::refreshRate(window_));
    std::fprintf(out, "window               %ld x %ld px\n", client.right, client.bottom);
    std::fprintf(out, "measured             %.1f s (after %.0f s warm-up)\n", seconds, kWarmUpSeconds);
    std::fprintf(out, "CPU                  %.2f %% of one core (process cycles, all threads)\n", cpu);
    std::fprintf(out, "frames               %llu (%.1f per second)\n", static_cast<unsigned long long>(frames_), frames_ / seconds);
    std::fprintf(out, "build per frame      avg %.3f ms, max %.3f ms (ImGui layout, CPU)\n", buildSum_ / frames, buildMax_);
    std::fprintf(out, "render per frame     avg %.3f ms, max %.3f ms (GL draw + present)\n", renderSum_ / frames, renderMax_);
    std::fprintf(out, "frames per second   ");
    for (const auto& s : perSecond_) std::fprintf(out, " %s", s.c_str());
    std::fprintf(out, "\n");
    std::fclose(out);
}

} // namespace afm::app
