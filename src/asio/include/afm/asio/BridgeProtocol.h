#pragma once

// Shared-memory protocol between the AudioFlow Matrix app (server, owns the clock) and
// the "AudioFlow Matrix ASIO" driver DLL loaded inside Traktor or a DAW (client).
//
//   app engine block k:  pull fromClient -> mix -> push toClient -> SetEvent(tick)
//   client thread:       wait(tick) -> read toClient -> host bufferSwitch -> write fromClient
//
// The client's output for block k is consumed by the engine in block k+1, so the bridge
// adds exactly one block of latency. Each ring is SPSC and each side only ever discards
// from the ring it consumes, so no reset ever races with the other process.

#include "afm/core/AudioRingCore.h"
#include "afm/core/Config.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>

namespace afm::bridge {

inline constexpr wchar_t kMappingName[] = L"Local\\AudioFlowMatrix.Bridge.v1";
inline constexpr wchar_t kTickEventName[] = L"Local\\AudioFlowMatrix.Tick.v1";
inline constexpr wchar_t kAppWindowClass[] = L"AudioFlowMatrixWindow";

inline constexpr std::uint32_t kMagic = 0x424D4641;  // "AFMB"
inline constexpr std::uint32_t kVersion = 1;
inline constexpr std::uint32_t kChannels = 8;         // per direction, as seen by the client
inline constexpr std::size_t kRingFrames = 8192;

enum : std::uint32_t { kServerStopped = 0, kServerRunning = 1 };
enum : std::uint32_t { kClientIdle = 0, kClientRunning = 1 };

struct alignas(kCacheLineSize) Header {
    std::uint32_t magic = 0;
    std::uint32_t version = 0;

    // Written by the server while serverState == kServerStopped, then published with a
    // release store of kServerRunning. Bumped generation tells the client to reset.
    std::atomic<std::uint32_t> serverState{kServerStopped};
    std::atomic<std::uint32_t> generation{0};
    double sampleRate = 0.0;
    std::uint32_t blockFrames = 0;
    std::uint32_t inputLatency = 0;   // master driver latency, frames
    std::uint32_t outputLatency = 0;

    alignas(kCacheLineSize) std::atomic<std::uint32_t> clientState{kClientIdle};
    std::atomic<std::uint32_t> clientPid{0};
    std::atomic<std::uint64_t> clientBlocks{0};
};

namespace detail {
constexpr std::size_t roundUp(std::size_t n, std::size_t a) noexcept { return (n + a - 1) / a * a; }
} // namespace detail

inline constexpr std::size_t kRingCapacity = AudioRingCore::roundCapacity(kRingFrames);
inline constexpr std::size_t kHeaderBytes = detail::roundUp(sizeof(Header), 4096);
inline constexpr std::size_t kRingCoreBytes = detail::roundUp(sizeof(AudioRingCore), kCacheLineSize);
inline constexpr std::size_t kRingBytes =
    kRingCoreBytes + AudioRingCore::storageFloats(kChannels, kRingCapacity) * sizeof(float);
inline constexpr std::size_t kToClientOffset = kHeaderBytes;
inline constexpr std::size_t kFromClientOffset = kHeaderBytes + detail::roundUp(kRingBytes, 4096);
inline constexpr std::size_t kTotalBytes = kFromClientOffset + detail::roundUp(kRingBytes, 4096);

struct SharedRing {
    AudioRingCore* core = nullptr;
    float* data = nullptr;
};

struct View {
    Header* header = nullptr;
    SharedRing toClient;    // server produces, client consumes (client's ASIO inputs)
    SharedRing fromClient;  // client produces, server consumes (client's ASIO outputs)
};

inline View viewOf(void* base) noexcept {
    auto* bytes = static_cast<std::byte*>(base);
    auto ring = [bytes](std::size_t offset) {
        return SharedRing{std::launder(reinterpret_cast<AudioRingCore*>(bytes + offset)),
                          reinterpret_cast<float*>(bytes + offset + kRingCoreBytes)};
    };
    return {std::launder(reinterpret_cast<Header*>(bytes)), ring(kToClientOffset), ring(kFromClientOffset)};
}

// Server only, on a freshly created (zero-filled) mapping.
inline View construct(void* base) noexcept {
    auto* bytes = static_cast<std::byte*>(base);
    auto* header = new (bytes) Header{};
    header->magic = kMagic;
    header->version = kVersion;
    new (bytes + kToClientOffset) AudioRingCore(kChannels, kRingCapacity);
    new (bytes + kFromClientOffset) AudioRingCore(kChannels, kRingCapacity);
    return viewOf(base);
}

[[nodiscard]] inline bool isValid(const void* base) noexcept {
    const auto* header = static_cast<const Header*>(base);
    return header->magic == kMagic && header->version == kVersion;
}

} // namespace afm::bridge
