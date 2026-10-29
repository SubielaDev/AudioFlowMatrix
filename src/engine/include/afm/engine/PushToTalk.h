#pragma once

#include <array>
#include <atomic>
#include <cstdint>

namespace afm {

// Push-to-talk state shared by the GUI thread (mouse), the MIDI callback thread and the
// audio thread. Lock-free, and owned outside the engine so it survives audio restarts.
//
// Hold:   open while any source (mouse button, MIDI pad) is held down.
// Toggle: each press flips it open/closed; repeated presses without a release (a CC
//         that re-sends its "on" value) count once.
class PushToTalk {
public:
    enum class Mode : std::uint8_t { Hold, Toggle };
    enum class Source : std::uint8_t { Mouse, Midi };

    void setEnabled(bool on) noexcept { enabled_.store(on, std::memory_order_relaxed); }
    [[nodiscard]] bool enabled() const noexcept { return enabled_.load(std::memory_order_relaxed); }

    // Switching mode starts closed, so a latch left over from Toggle never sticks open.
    void setMode(Mode mode) noexcept {
        mode_.store(mode, std::memory_order_relaxed);
        latched_.store(0, std::memory_order_relaxed);
    }
    [[nodiscard]] Mode mode() const noexcept { return mode_.load(std::memory_order_relaxed); }

    void press(Source source) noexcept {
        if (held(source).exchange(true, std::memory_order_relaxed)) return;
        if (mode() == Mode::Toggle) latched_.fetch_xor(1, std::memory_order_relaxed);
    }
    void release(Source source) noexcept { held(source).store(false, std::memory_order_relaxed); }

    // Whether mic -> stream passes right now (ignores enabled(); the engine checks both).
    [[nodiscard]] bool open() const noexcept {
        if (mode() == Mode::Toggle) return latched_.load(std::memory_order_relaxed) != 0;
        return held_[0].load(std::memory_order_relaxed) || held_[1].load(std::memory_order_relaxed);
    }

    // What the engine applies: the gate only acts while PTT is enabled.
    [[nodiscard]] bool passing() const noexcept { return !enabled() || open(); }

private:
    std::atomic<bool>& held(Source s) noexcept { return held_[static_cast<std::size_t>(s)]; }

    std::atomic<bool> enabled_{false};
    std::atomic<Mode> mode_{Mode::Hold};
    std::array<std::atomic<bool>, 2> held_{};
    std::atomic<std::uint32_t> latched_{0};
};

} // namespace afm
