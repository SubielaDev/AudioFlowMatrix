#pragma once

// Computer-keyboard input for the key shortcuts (push-to-talk, channel mutes), also while
// another program has the focus. Uses Raw Input on its own thread: it only reads the keys,
// never blocks or changes them, and installs no keyboard hook.

#include "afm/midi/KeyMap.h"

#include <atomic>
#include <string>
#include <thread>

namespace afm::app {

class KeyboardInput {
public:
    // Runs on the keyboard thread, one key change at a time (auto-repeat filtered out).
    using Handler = void (*)(void* user, const midi::KeyEvent& event) noexcept;

    KeyboardInput() = default;
    ~KeyboardInput() { stop(); }
    KeyboardInput(const KeyboardInput&) = delete;
    KeyboardInput& operator=(const KeyboardInput&) = delete;

    // False if Windows refused the registration (the reason goes to `error`).
    [[nodiscard]] bool start(Handler handler, void* user, std::string& error);
    // Blocks until the thread is gone; no handler calls arrive afterwards.
    void stop();

    [[nodiscard]] bool running() const noexcept { return thread_.joinable(); }

private:
    void run(std::atomic<int>& state, std::string& error);

    std::thread thread_;
    std::atomic<unsigned long> threadId_{0};
    Handler handler_ = nullptr;
    void* user_ = nullptr;
};

} // namespace afm::app
