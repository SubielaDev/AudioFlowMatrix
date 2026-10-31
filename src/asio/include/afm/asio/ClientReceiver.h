#pragma once

// App side of the fromClient ring (the client's ASIO outputs, e.g. Traktor's decks).
//
// The engine ticks the client at the end of block k and reads its answer at the start of
// block k+1, so the client has less than one block period to run its bufferSwitch. A client
// that is occasionally a little late used to cost a block of silence followed by a dropped
// block: a click. The receiver keeps a reserve ("safety") of whole client blocks instead:
// a late block is covered from the reserve and caught up on the next tick, without a gap.
//
// The reserve starts at kInitialSafety blocks (reported to the client as extra output
// latency). If the client is late even with the reserve, it grows by one block (up to
// kMaxSafety) and is rebuilt; every such event counts as a late block. A client that stays
// "running" but stops producing (e.g. a host ignoring a reset request) is reported as
// stalled and is not counted as late.

#include "afm/asio/BridgeProtocol.h"

#include <atomic>
#include <cstdint>
#include <cstring>

namespace afm::bridge {

class ClientReceiver {
public:
    static constexpr std::uint32_t kInitialSafety = 1;
    static constexpr std::uint32_t kMaxSafety = 4;
    static constexpr std::uint32_t kStallBlocks = 200;  // engine blocks without a new client block

    // Any thread: start over with the initial reserve (a new session, see begin()).
    void requestReset() noexcept { resetRequested_.store(true, std::memory_order_release); }

    // Engine thread. Fills dst with the client's audio, or with silence.
    void pull(SharedRing& ring, const Header& header, AudioBlock dst) noexcept {
        if (resetRequested_.exchange(false, std::memory_order_acq_rel)) {
            safety_.store(kInitialSafety, std::memory_order_relaxed);
            primed_ = false;
        }
        if (header.clientState.load(std::memory_order_acquire) != kClientRunning) {
            ring.discard(ring.readableFrames());
            primed_ = false;
            idleBlocks_ = 0;
            stalled_.store(false, std::memory_order_relaxed);
            silence(dst);
            return;
        }

        const std::uint64_t blocks = header.clientBlocks.load(std::memory_order_acquire);
        if (blocks != lastClientBlocks_) {
            lastClientBlocks_ = blocks;
            idleBlocks_ = 0;
            stalled_.store(false, std::memory_order_relaxed);
        } else if (idleBlocks_ < kStallBlocks) {
            ++idleBlocks_;
        }
        if (idleBlocks_ >= kStallBlocks) {
            stalled_.store(true, std::memory_order_relaxed);
            ring.discard(ring.readableFrames());
            primed_ = false;
            silence(dst);
            return;
        }

        const std::size_t block = dst.numFrames;
        const std::uint32_t safety = safety_.load(std::memory_order_relaxed);
        std::size_t available = ring.readableFrames();
        if (!primed_) {
            if (available < (safety + 1) * block) {  // still building the reserve
                silence(dst);
                return;
            }
            primed_ = true;
        }
        if (available > (safety + 2) * block) {  // a burst after a stall: keep the latency bounded
            ring.discard(available - (safety + 1) * block);
            available = (safety + 1) * block;
        }
        if (available < block) {  // late even with the reserve: grow it and rebuild
            late_.fetch_add(1, std::memory_order_relaxed);
            if (safety < kMaxSafety) safety_.store(safety + 1, std::memory_order_relaxed);
            primed_ = false;
            silence(dst);
            return;
        }
        ring.read(dst);
    }

    // Any thread.
    [[nodiscard]] std::uint32_t safetyBlocks() const noexcept { return safety_.load(std::memory_order_relaxed); }
    [[nodiscard]] std::uint64_t lateBlocks() const noexcept { return late_.load(std::memory_order_relaxed); }
    [[nodiscard]] bool stalled() const noexcept { return stalled_.load(std::memory_order_relaxed); }

private:
    static void silence(AudioBlock dst) noexcept {
        for (std::uint32_t ch = 0; ch < dst.numChannels; ++ch) {
            std::memset(dst.channels[ch], 0, dst.numFrames * sizeof(float));
        }
    }

    // Engine thread only.
    bool primed_ = false;
    std::uint64_t lastClientBlocks_ = 0;
    std::uint32_t idleBlocks_ = 0;

    std::atomic<bool> resetRequested_{false};
    std::atomic<std::uint32_t> safety_{kInitialSafety};
    std::atomic<std::uint64_t> late_{0};
    std::atomic<bool> stalled_{false};
};

} // namespace afm::bridge
