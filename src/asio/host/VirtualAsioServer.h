#pragma once

#include "afm/asio/BridgeProtocol.h"
#include "afm/engine/IAudioEndpoint.h"
#include "afm/platform/Win.h"

#include <cstdint>
#include <memory>
#include <string>

namespace afm::asio {

// App side of the virtual ASIO driver bridge. Owns the shared memory and the tick event
// and exposes the client's channels to the engine as one input endpoint (the client's
// ASIO outputs, e.g. Traktor decks) and one output endpoint (the client's ASIO inputs,
// e.g. DVS timecode).
class VirtualAsioServer {
public:
    VirtualAsioServer() = default;
    ~VirtualAsioServer();

    VirtualAsioServer(const VirtualAsioServer&) = delete;
    VirtualAsioServer& operator=(const VirtualAsioServer&) = delete;

    [[nodiscard]] bool open(std::string& error);
    void close();
    [[nodiscard]] bool isOpen() const noexcept { return base_ != nullptr; }

    // Control thread, around engine start/stop. begin() publishes the master format;
    // a connected client sees the new generation and asks its host to reset.
    void begin(double sampleRate, std::uint32_t blockFrames, std::uint32_t inputLatency, std::uint32_t outputLatency);
    void end();

    [[nodiscard]] std::shared_ptr<IInputEndpoint> inputEndpoint() const { return input_; }
    [[nodiscard]] std::shared_ptr<IOutputEndpoint> outputEndpoint() const { return output_; }

    [[nodiscard]] bool clientConnected() const noexcept;
    [[nodiscard]] std::uint32_t clientPid() const noexcept;
    [[nodiscard]] std::uint64_t clientBlocks() const noexcept;
    [[nodiscard]] std::uint64_t clientUnderruns() const noexcept;

private:
    class Input;
    class Output;

    HANDLE mapping_ = nullptr;
    HANDLE tick_ = nullptr;
    void* base_ = nullptr;
    bridge::View view_{};
    std::shared_ptr<IInputEndpoint> input_;
    std::shared_ptr<IOutputEndpoint> output_;
};

} // namespace afm::asio
