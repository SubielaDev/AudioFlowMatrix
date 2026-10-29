#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace afm::midi {

// One MIDI input port through WinMM (the API RtMidi itself uses on Windows).
//
// The handler runs on a WinMM-owned thread, one message at a time, also while the app
// window is minimised. It must not block or call back into WinMM.
class MidiInput {
public:
    struct Device {
        unsigned id = 0;
        std::string name;
    };

    using Handler = void (*)(void* user, std::uint32_t message, std::uint32_t timeMs) noexcept;

    MidiInput() = default;
    ~MidiInput() { close(); }
    MidiInput(const MidiInput&) = delete;
    MidiInput& operator=(const MidiInput&) = delete;

    [[nodiscard]] static std::vector<Device> list();

    // Opens the port and starts delivering short messages (notes, CCs, ...) to `handler`.
    [[nodiscard]] bool open(const Device& device, Handler handler, void* user, std::string& error);
    // Blocks until no handler call is in flight; no calls arrive afterwards.
    void close();

    [[nodiscard]] bool isOpen() const noexcept { return handle_ != nullptr; }
    [[nodiscard]] const std::string& deviceName() const noexcept { return name_; }

private:
    static void __stdcall callback(void* handle, unsigned msg, std::uintptr_t instance, std::uintptr_t param1,
                                   std::uintptr_t param2);

    void* handle_ = nullptr;  // HMIDIIN
    std::string name_;
    Handler handler_ = nullptr;
    void* user_ = nullptr;
};

} // namespace afm::midi
