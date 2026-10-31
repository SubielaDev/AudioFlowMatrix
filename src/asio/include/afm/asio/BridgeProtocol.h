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
//
// Trust: any process of the same user can write this memory, and the host may even run
// elevated. So nothing that sizes or indexes memory is read from it: ring geometry is a
// compile-time constant on both sides, every index is masked locally, cursor distances
// are clamped to the ring size, and header values are range-checked before use. A
// corrupted mapping can only produce wrong audio or silence, never an out-of-bounds access.

#include "afm/core/AudioTypes.h"
#include "afm/core/Config.h"

#include <algorithm>
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
inline constexpr std::uint32_t kVersion = 2;          // 2: geometry no longer stored in the mapping
inline constexpr std::uint32_t kChannels = 8;         // per direction, as seen by the client
inline constexpr std::size_t kRingFrames = 8192;      // power of two
inline constexpr std::size_t kRingMask = kRingFrames - 1;
static_assert((kRingFrames & kRingMask) == 0, "kRingFrames must be a power of two");
static_assert(kRingFrames >= 2 * kMaxBlockFrames, "a ring must hold at least two blocks");

// Header values outside these ranges are rejected by the client.
inline constexpr std::uint32_t kMinBlockFrames = 16;
inline constexpr double kMinSampleRate = 8000.0;
inline constexpr double kMaxSampleRate = 768000.0;

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

[[nodiscard]] inline bool validFormat(double sampleRate, std::uint32_t blockFrames) noexcept {
    return sampleRate >= kMinSampleRate && sampleRate <= kMaxSampleRate && blockFrames >= kMinBlockFrames &&
           blockFrames <= kMaxBlockFrames;
}

// The only per-ring state in shared memory: free-running frame counters.
struct RingCursors {
    alignas(kCacheLineSize) std::atomic<std::uint64_t> head{0};       // producer
    alignas(kCacheLineSize) std::atomic<std::uint64_t> tail{0};       // consumer
    alignas(kCacheLineSize) std::atomic<std::uint64_t> underruns{0};  // consumer
};

// Process-local view of one planar ring (kChannels lanes of kRingFrames floats).
class SharedRing {
public:
    SharedRing() noexcept = default;
    SharedRing(RingCursors* cursors, float* data) noexcept : c_(cursors), data_(data) {}

    // Consumer.
    [[nodiscard]] std::size_t readableFrames() noexcept {
        const std::uint64_t head = c_->head.load(std::memory_order_acquire);
        const std::uint64_t tail = c_->tail.load(std::memory_order_relaxed);
        const std::uint64_t used = head - tail;
        if (used > kRingFrames) {  // corrupted cursors: resynchronise to "empty"
            c_->tail.store(head, std::memory_order_release);
            return 0;
        }
        return static_cast<std::size_t>(used);
    }

    std::size_t discard(std::size_t frames) noexcept {
        const std::size_t n = std::min(frames, readableFrames());
        c_->tail.store(c_->tail.load(std::memory_order_relaxed) + n, std::memory_order_release);
        return n;
    }

    // Reads up to dst.numFrames (at most kMaxBlockFrames); the rest of dst is zeroed.
    std::size_t read(AudioBlock dst) noexcept {
        const std::size_t want = std::min<std::size_t>(dst.numFrames, kMaxBlockFrames);
        const std::size_t n = std::min(want, readableFrames());
        const std::uint64_t tail = c_->tail.load(std::memory_order_relaxed);
        for (std::uint32_t ch = 0; ch < dst.numChannels; ++ch) {
            float* out = dst.channels[ch];
            if (ch < kChannels) {
                copyOut(lane(ch), tail, out, n);
            } else {
                std::memset(out, 0, n * sizeof(float));
            }
            std::memset(out + n, 0, (dst.numFrames - n) * sizeof(float));
        }
        c_->tail.store(tail + n, std::memory_order_release);
        if (n < want) {
            c_->underruns.store(c_->underruns.load(std::memory_order_relaxed) + (want - n), std::memory_order_relaxed);
        }
        return n;
    }

    // Producer. Writes as much of src as fits; channels beyond src are silent.
    std::size_t write(ConstAudioBlock src) noexcept {
        const std::uint64_t head = c_->head.load(std::memory_order_relaxed);
        const std::uint64_t tail = c_->tail.load(std::memory_order_acquire);
        const std::uint64_t used = head - tail;
        const std::size_t space = used > kRingFrames ? 0 : static_cast<std::size_t>(kRingFrames - used);
        const std::size_t n = std::min({static_cast<std::size_t>(src.numFrames), space, std::size_t{kMaxBlockFrames}});
        if (n == 0) return 0;
        for (std::uint32_t ch = 0; ch < kChannels; ++ch) {
            copyIn(lane(ch), head, ch < src.numChannels ? src.channels[ch] : nullptr, n);
        }
        c_->head.store(head + n, std::memory_order_release);
        return n;
    }

    [[nodiscard]] std::uint64_t underruns() const noexcept { return c_->underruns.load(std::memory_order_relaxed); }

private:
    [[nodiscard]] float* lane(std::uint32_t ch) const noexcept { return data_ + std::size_t{ch} * kRingFrames; }

    static void copyOut(const float* lane, std::uint64_t position, float* dst, std::size_t n) noexcept {
        const auto idx = static_cast<std::size_t>(position & kRingMask);
        const std::size_t first = std::min(n, kRingFrames - idx);
        std::memcpy(dst, lane + idx, first * sizeof(float));
        std::memcpy(dst + first, lane, (n - first) * sizeof(float));
    }

    static void copyIn(float* lane, std::uint64_t position, const float* src, std::size_t n) noexcept {
        const auto idx = static_cast<std::size_t>(position & kRingMask);
        const std::size_t first = std::min(n, kRingFrames - idx);
        if (src != nullptr) {
            std::memcpy(lane + idx, src, first * sizeof(float));
            std::memcpy(lane, src + first, (n - first) * sizeof(float));
        } else {
            std::memset(lane + idx, 0, first * sizeof(float));
            std::memset(lane, 0, (n - first) * sizeof(float));
        }
    }

    RingCursors* c_ = nullptr;
    float* data_ = nullptr;
};

namespace detail {
constexpr std::size_t roundUp(std::size_t n, std::size_t a) noexcept { return (n + a - 1) / a * a; }
} // namespace detail

inline constexpr std::size_t kHeaderBytes = detail::roundUp(sizeof(Header), 4096);
inline constexpr std::size_t kCursorBytes = detail::roundUp(sizeof(RingCursors), kCacheLineSize);
inline constexpr std::size_t kRingBytes = kCursorBytes + std::size_t{kChannels} * kRingFrames * sizeof(float);
inline constexpr std::size_t kToClientOffset = kHeaderBytes;
inline constexpr std::size_t kFromClientOffset = kHeaderBytes + detail::roundUp(kRingBytes, 4096);
inline constexpr std::size_t kTotalBytes = kFromClientOffset + detail::roundUp(kRingBytes, 4096);

struct View {
    Header* header = nullptr;
    SharedRing toClient;    // server produces, client consumes (client's ASIO inputs)
    SharedRing fromClient;  // client produces, server consumes (client's ASIO outputs)
};

// `base` must be a view of at least kTotalBytes.
inline View viewOf(void* base) noexcept {
    auto* bytes = static_cast<std::byte*>(base);
    auto ring = [bytes](std::size_t offset) {
        return SharedRing{std::launder(reinterpret_cast<RingCursors*>(bytes + offset)),
                          reinterpret_cast<float*>(bytes + offset + kCursorBytes)};
    };
    return {std::launder(reinterpret_cast<Header*>(bytes)), ring(kToClientOffset), ring(kFromClientOffset)};
}

// Server only. Re-initialises the header and both rings.
inline View construct(void* base) noexcept {
    auto* bytes = static_cast<std::byte*>(base);
    auto* header = new (bytes) Header{};
    header->magic = kMagic;
    header->version = kVersion;
    new (bytes + kToClientOffset) RingCursors{};
    new (bytes + kFromClientOffset) RingCursors{};
    return viewOf(base);
}

[[nodiscard]] inline bool isValid(const void* base) noexcept {
    const auto* header = static_cast<const Header*>(base);
    return header->magic == kMagic && header->version == kVersion;
}

} // namespace afm::bridge
