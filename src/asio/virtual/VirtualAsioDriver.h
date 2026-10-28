#pragma once

#include "afm/asio/AsioAbi.h"
#include "afm/asio/BridgeProtocol.h"
#include "afm/platform/Win.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

namespace afm::vasio {

// {7C3E1A52-9B4D-4F6E-8A21-3D5C9E0B7F14}
inline constexpr CLSID kClsid = {0x7c3e1a52, 0x9b4d, 0x4f6e, {0x8a, 0x21, 0x3d, 0x5c, 0x9e, 0x0b, 0x7f, 0x14}};
inline constexpr char kDriverName[] = "AudioFlow Matrix ASIO";

extern std::atomic<long> g_objectCount;

// The driver object living inside the ASIO host process (Traktor, a DAW, ...). It has
// no clock of its own: a thread waits for the app's tick event and runs the host's
// bufferSwitch once per master block, moving audio through the shared-memory rings.
class VirtualAsioDriver final : public asio::IASIO {
public:
    VirtualAsioDriver();
    ~VirtualAsioDriver();

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;

    // IASIO
    asio::ASIOBool init(void* sysHandle) override;
    void getDriverName(char* name) override;
    long getDriverVersion() override;
    void getErrorMessage(char* string) override;
    asio::ASIOError start() override;
    asio::ASIOError stop() override;
    asio::ASIOError getChannels(long* numInputChannels, long* numOutputChannels) override;
    asio::ASIOError getLatencies(long* inputLatency, long* outputLatency) override;
    asio::ASIOError getBufferSize(long* minSize, long* maxSize, long* preferredSize, long* granularity) override;
    asio::ASIOError canSampleRate(asio::ASIOSampleRate sampleRate) override;
    asio::ASIOError getSampleRate(asio::ASIOSampleRate* sampleRate) override;
    asio::ASIOError setSampleRate(asio::ASIOSampleRate sampleRate) override;
    asio::ASIOError getClockSources(asio::ASIOClockSource* clocks, long* numSources) override;
    asio::ASIOError setClockSource(long reference) override;
    asio::ASIOError getSamplePosition(asio::ASIOSamples* sPos, asio::ASIOTimeStamp* tStamp) override;
    asio::ASIOError getChannelInfo(asio::ASIOChannelInfo* info) override;
    asio::ASIOError createBuffers(asio::ASIOBufferInfo* bufferInfos, long numChannels, long bufferSize,
                                  asio::ASIOCallbacks* callbacks) override;
    asio::ASIOError disposeBuffers() override;
    asio::ASIOError controlPanel() override;
    asio::ASIOError future(long selector, void* opt) override;
    asio::ASIOError outputReady() override;

private:
    static constexpr std::uint32_t kChannels = bridge::kChannels;

    void setError(const char* message) noexcept;
    void disconnect() noexcept;
    void audioThread() noexcept;
    void processBlock() noexcept;

    std::atomic<ULONG> refCount_{1};
    char error_[124] = {};

    HANDLE mapping_ = nullptr;
    HANDLE tick_ = nullptr;
    void* base_ = nullptr;
    bridge::View view_{};

    // Format snapshot taken at init(); a new server generation means "reset me".
    double sampleRate_ = 0.0;
    long blockFrames_ = 0;
    long inputLatency_ = 0;
    long outputLatency_ = 0;
    std::uint32_t generation_ = 0;

    // Host buffers: [direction][channel] -> two halves of blockFrames_ int32 samples.
    asio::ASIOCallbacks* callbacks_ = nullptr;
    bool useTimeInfo_ = false;
    std::vector<std::int32_t> hostBuffers_;
    std::array<std::int32_t*, kChannels * 2> inHalves_{};   // client inputs  (engine -> host)
    std::array<std::int32_t*, kChannels * 2> outHalves_{};  // client outputs (host -> engine)
    std::vector<float> scratch_;
    std::array<float*, kChannels> scratchIn_{};
    std::array<float*, kChannels> scratchOut_{};
    asio::ASIOTime asioTime_{};

    std::thread thread_;
    std::atomic<bool> running_{false};
    long bufferIndex_ = 0;
    bool resetSent_ = false;
    std::atomic<std::uint64_t> samplePosition_{0};
    std::atomic<std::uint64_t> timestampNs_{0};
};

} // namespace afm::vasio
