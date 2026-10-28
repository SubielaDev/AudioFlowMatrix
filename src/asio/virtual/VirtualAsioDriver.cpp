#include "VirtualAsioDriver.h"

#include "afm/asio/AsioSampleConvert.h"
#include "afm/core/Denormals.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace afm::vasio {

using namespace afm::asio;

std::atomic<long> g_objectCount{0};

VirtualAsioDriver::VirtualAsioDriver() { g_objectCount.fetch_add(1); }

VirtualAsioDriver::~VirtualAsioDriver() {
    disposeBuffers();
    disconnect();
    g_objectCount.fetch_sub(1);
}

// ---- IUnknown -----------------------------------------------------------------------

HRESULT STDMETHODCALLTYPE VirtualAsioDriver::QueryInterface(REFIID riid, void** object) {
    if (object == nullptr) {
        return E_POINTER;
    }
    // ASIO hosts ask for the driver's CLSID as the interface id.
    if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, kClsid)) {
        *object = static_cast<IASIO*>(this);
        AddRef();
        return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE VirtualAsioDriver::AddRef() { return refCount_.fetch_add(1) + 1; }

ULONG STDMETHODCALLTYPE VirtualAsioDriver::Release() {
    const ULONG count = refCount_.fetch_sub(1) - 1;
    if (count == 0) {
        delete this;
    }
    return count;
}

// ---- Connection -----------------------------------------------------------------------

void VirtualAsioDriver::setError(const char* message) noexcept {
    std::snprintf(error_, sizeof(error_), "%s", message);
}

void VirtualAsioDriver::disconnect() noexcept {
    if (base_ != nullptr) {
        UnmapViewOfFile(base_);
        base_ = nullptr;
    }
    if (mapping_ != nullptr) {
        CloseHandle(mapping_);
        mapping_ = nullptr;
    }
    if (tick_ != nullptr) {
        CloseHandle(tick_);
        tick_ = nullptr;
    }
    view_ = {};
}

ASIOBool VirtualAsioDriver::init(void*) {
    if (base_ == nullptr) {
        mapping_ = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, bridge::kMappingName);
        if (mapping_ == nullptr) {
            setError("AudioFlow Matrix is not running. Start AudioFlow Matrix first.");
            return ASIOFalse;
        }
        base_ = MapViewOfFile(mapping_, FILE_MAP_ALL_ACCESS, 0, 0, bridge::kTotalBytes);
        if (base_ == nullptr || !bridge::isValid(base_)) {
            setError("AudioFlow Matrix version mismatch. Reinstall the driver.");
            disconnect();
            return ASIOFalse;
        }
        view_ = bridge::viewOf(base_);
        tick_ = OpenEventW(SYNCHRONIZE, FALSE, bridge::kTickEventName);
        if (tick_ == nullptr) {
            setError("Could not connect to the AudioFlow Matrix clock.");
            disconnect();
            return ASIOFalse;
        }
    }

    const bridge::Header& h = *view_.header;
    if (h.serverState.load(std::memory_order_acquire) != bridge::kServerRunning) {
        setError("The AudioFlow Matrix audio engine is stopped. Press Start in AudioFlow Matrix.");
        return ASIOFalse;
    }
    generation_ = h.generation.load(std::memory_order_acquire);
    sampleRate_ = h.sampleRate;
    blockFrames_ = static_cast<long>(h.blockFrames);
    inputLatency_ = static_cast<long>(h.inputLatency);
    outputLatency_ = static_cast<long>(h.outputLatency);
    error_[0] = '\0';
    return ASIOTrue;
}

void VirtualAsioDriver::getDriverName(char* name) { std::snprintf(name, 32, "%s", kDriverName); }

long VirtualAsioDriver::getDriverVersion() { return 1; }

void VirtualAsioDriver::getErrorMessage(char* string) { std::snprintf(string, 124, "%s", error_); }

// ---- Format -----------------------------------------------------------------------------

ASIOError VirtualAsioDriver::getChannels(long* numInputChannels, long* numOutputChannels) {
    if (base_ == nullptr) return ASE_NotPresent;
    if (numInputChannels) *numInputChannels = kChannels;
    if (numOutputChannels) *numOutputChannels = kChannels;
    return ASE_OK;
}

ASIOError VirtualAsioDriver::getLatencies(long* inputLatency, long* outputLatency) {
    if (base_ == nullptr) return ASE_NotPresent;
    // One block for the bridge plus whatever the master driver reports.
    if (inputLatency) *inputLatency = blockFrames_ + inputLatency_;
    if (outputLatency) *outputLatency = blockFrames_ + outputLatency_;
    return ASE_OK;
}

ASIOError VirtualAsioDriver::getBufferSize(long* minSize, long* maxSize, long* preferredSize, long* granularity) {
    if (base_ == nullptr) return ASE_NotPresent;
    // The block size is dictated by the master driver.
    if (minSize) *minSize = blockFrames_;
    if (maxSize) *maxSize = blockFrames_;
    if (preferredSize) *preferredSize = blockFrames_;
    if (granularity) *granularity = 0;
    return ASE_OK;
}

ASIOError VirtualAsioDriver::canSampleRate(ASIOSampleRate sampleRate) {
    if (base_ == nullptr) return ASE_NotPresent;
    return std::fabs(sampleRate - sampleRate_) < 0.5 ? ASE_OK : ASE_NoClock;
}

ASIOError VirtualAsioDriver::getSampleRate(ASIOSampleRate* sampleRate) {
    if (base_ == nullptr) return ASE_NotPresent;
    if (sampleRate) *sampleRate = sampleRate_;
    return ASE_OK;
}

ASIOError VirtualAsioDriver::setSampleRate(ASIOSampleRate sampleRate) { return canSampleRate(sampleRate); }

ASIOError VirtualAsioDriver::getClockSources(ASIOClockSource* clocks, long* numSources) {
    if (clocks == nullptr || numSources == nullptr) return ASE_InvalidParameter;
    if (*numSources >= 1) {
        clocks[0] = {};
        clocks[0].index = 0;
        clocks[0].associatedChannel = -1;
        clocks[0].associatedGroup = -1;
        clocks[0].isCurrentSource = ASIOTrue;
        std::snprintf(clocks[0].name, sizeof(clocks[0].name), "AudioFlow Matrix master");
    }
    *numSources = 1;
    return ASE_OK;
}

ASIOError VirtualAsioDriver::setClockSource(long reference) { return reference == 0 ? ASE_OK : ASE_InvalidParameter; }

ASIOError VirtualAsioDriver::getSamplePosition(ASIOSamples* sPos, ASIOTimeStamp* tStamp) {
    if (sPos) *sPos = toAsioSamples(samplePosition_.load(std::memory_order_acquire));
    if (tStamp) *tStamp = toAsioTimeStamp(timestampNs_.load(std::memory_order_acquire));
    return ASE_OK;
}

ASIOError VirtualAsioDriver::getChannelInfo(ASIOChannelInfo* info) {
    if (info == nullptr || info->channel < 0 || info->channel >= static_cast<long>(kChannels)) {
        return ASE_InvalidParameter;
    }
    const auto ch = static_cast<std::size_t>(info->channel);
    const bool isInput = info->isInput != ASIOFalse;
    info->isActive = (isInput ? inHalves_[ch * 2] : outHalves_[ch * 2]) != nullptr ? ASIOTrue : ASIOFalse;
    info->channelGroup = 0;
    info->type = ASIOSTInt32LSB;
    std::snprintf(info->name, sizeof(info->name), "AFM %s %ld", isInput ? "In" : "Out", info->channel + 1);
    return ASE_OK;
}

// ---- Buffers ----------------------------------------------------------------------------

ASIOError VirtualAsioDriver::createBuffers(ASIOBufferInfo* bufferInfos, long numChannels, long bufferSize,
                                           ASIOCallbacks* callbacks) {
    if (base_ == nullptr) return ASE_NotPresent;
    if (bufferInfos == nullptr || callbacks == nullptr || numChannels <= 0) return ASE_InvalidParameter;
    if (bufferSize != blockFrames_) return ASE_InvalidMode;
    disposeBuffers();

    for (long i = 0; i < numChannels; ++i) {
        if (bufferInfos[i].channelNum < 0 || bufferInfos[i].channelNum >= static_cast<long>(kChannels)) {
            return ASE_InvalidParameter;
        }
    }

    const auto frames = static_cast<std::size_t>(blockFrames_);
    hostBuffers_.assign(static_cast<std::size_t>(numChannels) * 2 * frames, 0);
    for (long i = 0; i < numChannels; ++i) {
        std::int32_t* half0 = hostBuffers_.data() + static_cast<std::size_t>(i) * 2 * frames;
        std::int32_t* half1 = half0 + frames;
        bufferInfos[i].buffers[0] = half0;
        bufferInfos[i].buffers[1] = half1;
        const auto ch = static_cast<std::size_t>(bufferInfos[i].channelNum);
        auto& halves = bufferInfos[i].isInput != ASIOFalse ? inHalves_ : outHalves_;
        halves[ch * 2] = half0;
        halves[ch * 2 + 1] = half1;
    }

    scratch_.assign(kChannels * 2 * frames, 0.0f);
    for (std::uint32_t ch = 0; ch < kChannels; ++ch) {
        scratchIn_[ch] = scratch_.data() + ch * frames;
        scratchOut_[ch] = scratch_.data() + (kChannels + ch) * frames;
    }

    callbacks_ = callbacks;
    useTimeInfo_ = callbacks->asioMessage != nullptr && callbacks->bufferSwitchTimeInfo != nullptr &&
                   callbacks->asioMessage(kAsioSelectorSupported, kAsioSupportsTimeInfo, nullptr, nullptr) == 1 &&
                   callbacks->asioMessage(kAsioSupportsTimeInfo, 0, nullptr, nullptr) == 1;
    return ASE_OK;
}

ASIOError VirtualAsioDriver::disposeBuffers() {
    stop();
    callbacks_ = nullptr;
    hostBuffers_.clear();
    inHalves_.fill(nullptr);
    outHalves_.fill(nullptr);
    return ASE_OK;
}

// ---- Streaming ----------------------------------------------------------------------------

ASIOError VirtualAsioDriver::start() {
    if (callbacks_ == nullptr) return ASE_InvalidMode;
    if (running_) return ASE_OK;

    // We are the consumer of toClient: drop whatever is stale from a previous session.
    view_.toClient.core->discard(view_.toClient.core->readableFrames());
    samplePosition_.store(0);
    bufferIndex_ = 0;
    resetSent_ = false;

    bridge::Header& h = *view_.header;
    h.clientPid.store(GetCurrentProcessId(), std::memory_order_relaxed);
    h.clientState.store(bridge::kClientRunning, std::memory_order_release);

    running_.store(true, std::memory_order_release);
    thread_ = std::thread([this] { audioThread(); });
    return ASE_OK;
}

ASIOError VirtualAsioDriver::stop() {
    if (!running_.exchange(false)) {
        return ASE_OK;
    }
    if (thread_.joinable()) {
        thread_.join();
    }
    if (view_.header != nullptr) {
        view_.header->clientState.store(bridge::kClientIdle, std::memory_order_release);
    }
    return ASE_OK;
}

void VirtualAsioDriver::audioThread() noexcept {
    const win::ScopedMmcss mmcss;
    const ScopedFlushDenormals ftz;
    const bridge::Header& h = *view_.header;

    while (running_.load(std::memory_order_acquire)) {
        const DWORD r = WaitForSingleObject(tick_, 100);
        if (!running_.load(std::memory_order_acquire)) {
            break;
        }
        if (r != WAIT_OBJECT_0) {
            continue;  // engine paused; keep waiting
        }
        if (h.serverState.load(std::memory_order_acquire) != bridge::kServerRunning ||
            h.generation.load(std::memory_order_acquire) != generation_) {
            // The master format may have changed: ask the host to tear down and re-init.
            if (!resetSent_ && callbacks_->asioMessage != nullptr) {
                resetSent_ = true;
                callbacks_->asioMessage(kAsioResetRequest, 0, nullptr, nullptr);
            }
            continue;
        }
        processBlock();
    }
}

void VirtualAsioDriver::processBlock() noexcept {
    const long frames = blockFrames_;
    const long index = bufferIndex_;
    bufferIndex_ ^= 1;

    // Engine -> host inputs. Keep only the newest block if we fell behind.
    AudioRingCore& rx = *view_.toClient.core;
    const std::size_t available = rx.readableFrames();
    if (available > static_cast<std::size_t>(frames)) {
        rx.discard(available - static_cast<std::size_t>(frames));
    }
    rx.read(view_.toClient.data, AudioBlock{scratchIn_.data(), kChannels, static_cast<std::uint32_t>(frames)});
    for (std::uint32_t ch = 0; ch < kChannels; ++ch) {
        if (std::int32_t* dst = inHalves_[ch * 2 + static_cast<std::size_t>(index)]) {
            fromFloat(ASIOSTInt32LSB, scratchIn_[ch], dst, frames);
        }
    }

    const std::uint64_t position = samplePosition_.load(std::memory_order_relaxed);
    const std::uint64_t now = win::nowNanoseconds();
    timestampNs_.store(now, std::memory_order_release);

    if (useTimeInfo_) {
        asioTime_ = {};
        asioTime_.timeInfo.speed = 1.0;
        asioTime_.timeInfo.systemTime = toAsioTimeStamp(now);
        asioTime_.timeInfo.samplePosition = toAsioSamples(position);
        asioTime_.timeInfo.sampleRate = sampleRate_;
        asioTime_.timeInfo.flags = kSystemTimeValid | kSamplePositionValid | kSampleRateValid;
        callbacks_->bufferSwitchTimeInfo(&asioTime_, index, ASIOTrue);
    } else {
        callbacks_->bufferSwitch(index, ASIOTrue);
    }

    // Host outputs -> engine.
    for (std::uint32_t ch = 0; ch < kChannels; ++ch) {
        if (const std::int32_t* src = outHalves_[ch * 2 + static_cast<std::size_t>(index)]) {
            toFloat(ASIOSTInt32LSB, src, scratchOut_[ch], frames);
        } else {
            std::memset(scratchOut_[ch], 0, static_cast<std::size_t>(frames) * sizeof(float));
        }
    }
    view_.fromClient.core->write(view_.fromClient.data,
                                 ConstAudioBlock{scratchOut_.data(), kChannels, static_cast<std::uint32_t>(frames)});

    samplePosition_.store(position + static_cast<std::uint64_t>(frames), std::memory_order_release);
    bridge::Header& h = *view_.header;
    h.clientBlocks.store(h.clientBlocks.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
}

// ---- Misc ---------------------------------------------------------------------------------

ASIOError VirtualAsioDriver::controlPanel() {
    // Routing lives in the app: bring its window to the front.
    if (HWND app = FindWindowW(bridge::kAppWindowClass, nullptr)) {
        ShowWindow(app, SW_RESTORE);
        SetForegroundWindow(app);
        return ASE_OK;
    }
    MessageBoxW(nullptr, L"Open AudioFlow Matrix to configure routing.", L"AudioFlow Matrix ASIO",
                MB_OK | MB_ICONINFORMATION);
    return ASE_OK;
}

ASIOError VirtualAsioDriver::future(long, void*) { return ASE_InvalidParameter; }

ASIOError VirtualAsioDriver::outputReady() { return ASE_NotPresent; }

} // namespace afm::vasio
