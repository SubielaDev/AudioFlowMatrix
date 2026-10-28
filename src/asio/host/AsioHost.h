#pragma once

#include "afm/asio/AsioAbi.h"
#include "afm/core/AlignedBuffer.h"
#include "afm/platform/Win.h"

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace afm {
class AudioEngine;
}

namespace afm::asio {

struct DriverEntry {
    std::string name;
    CLSID clsid{};
};

// Installed ASIO drivers from HKLM\SOFTWARE\ASIO, excluding our own virtual driver.
[[nodiscard]] std::vector<DriverEntry> listDrivers();

struct ChannelDesc {
    std::string name;
    ASIOSampleType type = ASIOSTInt32LSB;
};

// Hosts the physical (master) ASIO driver. The driver's bufferSwitch callback is the
// heartbeat of the whole application: it converts the driver buffers to float, runs
// AudioEngine::process() and converts back.
//
// All public methods must be called from the thread that called load() (an STA thread,
// the GUI thread in the app): ASIO drivers are not thread-safe.
class AsioHost {
public:
    AsioHost() = default;
    ~AsioHost();

    AsioHost(const AsioHost&) = delete;
    AsioHost& operator=(const AsioHost&) = delete;

    // Instantiates and initialises the driver, then reads its format. If
    // `preferredSampleRate` > 0 and the driver supports it, switches to that rate.
    [[nodiscard]] bool load(const DriverEntry& driver, HWND window, double preferredSampleRate, std::string& error);
    void unload();
    [[nodiscard]] bool isLoaded() const noexcept { return driver_ != nullptr; }

    // Format of the loaded driver.
    [[nodiscard]] const std::string& driverName() const noexcept { return name_; }
    [[nodiscard]] double sampleRate() const noexcept { return sampleRate_; }
    [[nodiscard]] std::uint32_t blockFrames() const noexcept { return blockFrames_; }
    [[nodiscard]] const std::vector<ChannelDesc>& inputs() const noexcept { return inputs_; }
    [[nodiscard]] const std::vector<ChannelDesc>& outputs() const noexcept { return outputs_; }
    [[nodiscard]] long inputLatency() const noexcept { return inputLatency_; }
    [[nodiscard]] long outputLatency() const noexcept { return outputLatency_; }

    // The engine must already be prepared for inputs().size() / outputs().size()
    // hardware channels at blockFrames().
    [[nodiscard]] bool start(AudioEngine& engine, std::string& error);
    void stop();
    [[nodiscard]] bool isRunning() const noexcept { return running_; }

    void openControlPanel();

    // Set from driver callbacks; the owner polls this and restarts the stream.
    [[nodiscard]] bool takeResetRequest() noexcept { return resetRequested_.exchange(false); }
    [[nodiscard]] std::uint64_t callbackCount() const noexcept { return callbacks_.load(std::memory_order_relaxed); }
    [[nodiscard]] std::uint64_t overloadCount() const noexcept { return overloads_.load(std::memory_order_relaxed); }

private:
    static void onBufferSwitch(long index, ASIOBool directProcess);
    static ASIOTime* onBufferSwitchTimeInfo(ASIOTime* params, long index, ASIOBool directProcess);
    static void onSampleRateChanged(ASIOSampleRate rate);
    static long onAsioMessage(long selector, long value, void* message, double* opt);

    void process(long index) noexcept;
    bool readFormat(std::string& error);

    static AsioHost* s_active;

    IASIO* driver_ = nullptr;
    std::string name_;
    double sampleRate_ = 0.0;
    std::uint32_t blockFrames_ = 0;
    long inputLatency_ = 0;
    long outputLatency_ = 0;
    std::vector<ChannelDesc> inputs_;
    std::vector<ChannelDesc> outputs_;

    bool buffersCreated_ = false;
    bool running_ = false;
    bool postOutput_ = false;
    ASIOCallbacks callbackTable_{};
    std::vector<ASIOBufferInfo> bufferInfos_;
    std::vector<ASIOSampleType> types_;
    AlignedBuffer<float, kSimdAlignment> scratch_;
    std::vector<float*> inLanes_;
    std::vector<float*> outLanes_;
    AudioEngine* engine_ = nullptr;

    std::atomic<bool> resetRequested_{false};
    std::atomic<std::uint64_t> callbacks_{0};
    std::atomic<std::uint64_t> overloads_{0};
};

} // namespace afm::asio
