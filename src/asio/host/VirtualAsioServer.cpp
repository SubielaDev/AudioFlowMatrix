#include "VirtualAsioServer.h"

#include <cstring>

namespace afm::asio {

namespace {

bool clientRunning(const bridge::Header& h) noexcept {
    return h.clientState.load(std::memory_order_acquire) == bridge::kClientRunning;
}

} // namespace

// Client ASIO outputs -> matrix inputs.
class VirtualAsioServer::Input final : public IInputEndpoint {
public:
    explicit Input(const bridge::View& view) : view_(view) {}

    std::string_view name() const noexcept override { return "AudioFlow ASIO"; }
    std::uint32_t numChannels() const noexcept override { return bridge::kChannels; }
    std::string channelName(std::uint32_t ch) const override { return "ASIO Client Out " + std::to_string(ch + 1); }

    void pull(AudioBlock dst) noexcept override {
        AudioRingCore& ring = *view_.fromClient.core;
        if (!clientRunning(*view_.header)) {
            ring.discard(ring.readableFrames());
            for (std::uint32_t ch = 0; ch < dst.numChannels; ++ch) {
                std::memset(dst.channels[ch], 0, dst.numFrames * sizeof(float));
            }
            return;
        }
        // The client produces exactly one block per tick. If it fell behind and then
        // caught up, keep only the newest block so latency never accumulates.
        const std::size_t available = ring.readableFrames();
        if (available > dst.numFrames) {
            ring.discard(available - dst.numFrames);
        }
        ring.read(view_.fromClient.data, dst);
    }

private:
    bridge::View view_;
};

// Matrix outputs -> client ASIO inputs.
class VirtualAsioServer::Output final : public IOutputEndpoint {
public:
    Output(const bridge::View& view, HANDLE tick) : view_(view), tick_(tick) {}

    std::string_view name() const noexcept override { return "AudioFlow ASIO"; }
    std::uint32_t numChannels() const noexcept override { return bridge::kChannels; }
    std::string channelName(std::uint32_t ch) const override { return "ASIO Client In " + std::to_string(ch + 1); }

    void push(ConstAudioBlock src) noexcept override {
        if (!clientRunning(*view_.header)) {
            return;
        }
        view_.toClient.core->write(view_.toClient.data, src);
        SetEvent(tick_);  // non-blocking; wakes the client's audio thread
    }

private:
    bridge::View view_;
    HANDLE tick_;
};

VirtualAsioServer::~VirtualAsioServer() { close(); }

bool VirtualAsioServer::open(std::string& error) {
    if (isOpen()) {
        return true;
    }
    mapping_ = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                  static_cast<DWORD>(bridge::kTotalBytes), bridge::kMappingName);
    if (mapping_ == nullptr) {
        error = "Could not create the virtual ASIO shared memory (error " + std::to_string(GetLastError()) + ")";
        return false;
    }
    const bool existed = GetLastError() == ERROR_ALREADY_EXISTS;
    base_ = MapViewOfFile(mapping_, FILE_MAP_ALL_ACCESS, 0, 0, bridge::kTotalBytes);
    if (base_ == nullptr) {
        error = "Could not map the virtual ASIO shared memory";
        close();
        return false;
    }
    // A client that outlived a previous app instance keeps the mapping alive; reuse it.
    view_ = (existed && bridge::isValid(base_)) ? bridge::viewOf(base_) : bridge::construct(base_);

    tick_ = CreateEventW(nullptr, FALSE, FALSE, bridge::kTickEventName);
    if (tick_ == nullptr) {
        error = "Could not create the virtual ASIO tick event";
        close();
        return false;
    }
    input_ = std::make_shared<Input>(view_);
    output_ = std::make_shared<Output>(view_, tick_);
    return true;
}

void VirtualAsioServer::close() {
    if (base_ != nullptr) {
        end();
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
    input_.reset();
    output_.reset();
    view_ = {};
}

void VirtualAsioServer::begin(double sampleRate, std::uint32_t blockFrames, std::uint32_t inputLatency,
                              std::uint32_t outputLatency) {
    if (!isOpen()) {
        return;
    }
    bridge::Header& h = *view_.header;
    h.serverState.store(bridge::kServerStopped, std::memory_order_release);
    h.sampleRate = sampleRate;
    h.blockFrames = blockFrames;
    h.inputLatency = inputLatency;
    h.outputLatency = outputLatency;
    h.generation.store(h.generation.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
    h.serverState.store(bridge::kServerRunning, std::memory_order_release);
}

void VirtualAsioServer::end() {
    if (isOpen()) {
        view_.header->serverState.store(bridge::kServerStopped, std::memory_order_release);
    }
}

bool VirtualAsioServer::clientConnected() const noexcept {
    return isOpen() && clientRunning(*view_.header);
}

std::uint32_t VirtualAsioServer::clientPid() const noexcept {
    return isOpen() ? view_.header->clientPid.load(std::memory_order_relaxed) : 0;
}

std::uint64_t VirtualAsioServer::clientBlocks() const noexcept {
    return isOpen() ? view_.header->clientBlocks.load(std::memory_order_relaxed) : 0;
}

std::uint64_t VirtualAsioServer::clientUnderruns() const noexcept {
    return isOpen() ? view_.fromClient.core->underruns() : 0;
}

} // namespace afm::asio
