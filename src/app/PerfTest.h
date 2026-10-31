#pragma once

// Performance test of the GUI render loop:
//
//   AudioFlowMatrix.exe --perf-test=20 --render=vsync|cap60|display [--perf-scene=silent] [--perf-tab=matrix|devices]
//
// Shows a fixed fake layout with synthetic meters (App PerfScene), measures for the given
// number of seconds after a 2 s warm-up, writes a report to the logs folder
// (perf-<mode>-<scene>-<time>.log) and closes.

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

namespace afm::app {

enum class RenderMode {
    VSync,    // redraw at the monitor refresh rate (v0.3.1 and before)
    Cap60,    // at most 60 frames per second (the default)
    Display,  // paced by the timer at the monitor refresh rate (Settings > Match the display)
};

[[nodiscard]] const char* renderModeName(RenderMode mode);

class PerfRecorder {
public:
    PerfRecorder(RenderMode mode, bool movingMeters, double seconds, HWND window);

    // One GUI frame: time to build it (ImGui, CPU) and to render + present it.
    void frame(double buildMs, double renderMs);
    // True once the test time has passed; the report is then written.
    [[nodiscard]] bool finished();

private:
    void reset(LONGLONG now);
    void writeReport(double seconds);

    RenderMode mode_;
    bool moving_;
    double seconds_;
    HWND window_;
    LONGLONG frequency_ = 1;
    LONGLONG start_ = 0;
    LONGLONG secondStart_ = 0;
    bool warm_ = false;
    bool done_ = false;
    std::uint64_t cycles0_ = 0;

    std::uint64_t frames_ = 0;
    double buildSum_ = 0, buildMax_ = 0;
    double renderSum_ = 0, renderMax_ = 0;
    std::uint64_t secondFrames_ = 0;
    std::vector<std::string> perSecond_;
};

} // namespace afm::app
