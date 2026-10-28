#pragma once

#include "afm/core/AudioTypes.h"
#include "afm/core/Config.h"
#include "afm/core/SpscCursor.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace afm {

// Lock-free SPSC FIFO logic for planar multichannel float audio, separated from its
// storage so the same code drives a heap ring (AudioRingBuffer) and a ring that lives in
// shared memory between two processes (the virtual ASIO bridge).
//
// Storage layout: `numChannels` contiguous lanes of `capacityFrames` floats. Head/tail
// count frames, so a frame is always published atomically across all channels.
//
// Glitch policy (never blocks, never allocates):
//   - write() on a full ring drops the frames that don't fit and counts them as overrun.
//   - read() on a short ring zero-fills the missing frames and counts them as underrun.
class AudioRingCore {
public:
    // Capacity is rounded up to a power of two, minimum 64 frames so every lane starts
    // on a 32-byte (AVX2) boundary.
    [[nodiscard]] static constexpr std::size_t roundCapacity(std::size_t minFrames) noexcept {
        return std::bit_ceil(std::max<std::size_t>(minFrames, 64));
    }

    [[nodiscard]] static constexpr std::size_t storageFloats(std::uint32_t numChannels,
                                                             std::size_t capacityFrames) noexcept {
        return static_cast<std::size_t>(numChannels) * capacityFrames;
    }

    // `capacityFrames` must already be a value returned by roundCapacity().
    AudioRingCore(std::uint32_t numChannels, std::size_t capacityFrames) noexcept
        : cursor_(capacityFrames), numChannels_(numChannels) {}

    AudioRingCore(const AudioRingCore&) = delete;
    AudioRingCore& operator=(const AudioRingCore&) = delete;

    [[nodiscard]] std::uint32_t numChannels() const noexcept { return numChannels_; }
    [[nodiscard]] std::size_t capacityFrames() const noexcept { return cursor_.capacity(); }

    // Approximate from any thread; exact from the consumer.
    [[nodiscard]] std::size_t fillLevelFrames() const noexcept { return cursor_.sizeApprox(); }

    // ---- Producer -------------------------------------------------------------------

    [[nodiscard]] std::size_t writableFrames() noexcept { return cursor_.claimWrite(capacityFrames()); }

    // Channels of `src` beyond numChannels() are ignored; missing ones are written as
    // silence. Returns frames written.
    std::size_t write(float* storage, ConstAudioBlock src) noexcept {
        const std::size_t n = cursor_.claimWrite(src.numFrames);
        if (n != 0) {
            const std::size_t pos = cursor_.writeIndex();
            for (std::uint32_t ch = 0; ch < numChannels_; ++ch) {
                if (ch < src.numChannels) {
                    copyIntoLane(lane(storage, ch), pos, src.channels[ch], n);
                } else {
                    zeroLane(lane(storage, ch), pos, n);
                }
            }
            cursor_.publishWrite(n);
        }
        countDropped(overruns_, src.numFrames - n);
        return n;
    }

    // Pre-roll: pad the ring with silence to set the initial latency.
    std::size_t writeSilence(float* storage, std::size_t frames) noexcept {
        const std::size_t n = cursor_.claimWrite(frames);
        if (n != 0) {
            const std::size_t pos = cursor_.writeIndex();
            for (std::uint32_t ch = 0; ch < numChannels_; ++ch) {
                zeroLane(lane(storage, ch), pos, n);
            }
            cursor_.publishWrite(n);
        }
        return n;
    }

    [[nodiscard]] std::uint64_t overruns() const noexcept { return overruns_.load(std::memory_order_relaxed); }

    // ---- Consumer -------------------------------------------------------------------

    [[nodiscard]] std::size_t readableFrames() noexcept { return cursor_.claimRead(capacityFrames()); }

    // Always produces exactly dst.numFrames frames on every dst channel; the part the
    // ring could not supply is silence. Returns frames actually read from the ring.
    std::size_t read(const float* storage, AudioBlock dst) noexcept {
        const std::size_t n = cursor_.claimRead(dst.numFrames);
        const std::size_t pos = cursor_.readIndex();
        const std::size_t shortfall = dst.numFrames - n;
        for (std::uint32_t ch = 0; ch < dst.numChannels; ++ch) {
            float* out = dst.channels[ch];
            if (ch < numChannels_) {
                copyFromLane(lane(storage, ch), pos, out, n);
            } else {
                std::memset(out, 0, n * sizeof(float));
            }
            if (shortfall != 0) {
                std::memset(out + n, 0, shortfall * sizeof(float));
            }
        }
        if (n != 0) {
            cursor_.publishRead(n);
        }
        countDropped(underruns_, shortfall);
        return n;
    }

    // Skips up to `frames` frames (drift correction / latency trim). Returns frames skipped.
    std::size_t discard(std::size_t frames) noexcept {
        const std::size_t n = cursor_.claimRead(frames);
        cursor_.publishRead(n);
        return n;
    }

    [[nodiscard]] std::uint64_t underruns() const noexcept { return underruns_.load(std::memory_order_relaxed); }

    // ---- Control --------------------------------------------------------------------

    // Not thread-safe: only call while neither side is running. Does not touch storage.
    void reset() noexcept {
        cursor_.reset();
        overruns_.store(0, std::memory_order_relaxed);
        underruns_.store(0, std::memory_order_relaxed);
    }

private:
    [[nodiscard]] float* lane(float* storage, std::uint32_t ch) const noexcept {
        return storage + static_cast<std::size_t>(ch) * cursor_.capacity();
    }
    [[nodiscard]] const float* lane(const float* storage, std::uint32_t ch) const noexcept {
        return storage + static_cast<std::size_t>(ch) * cursor_.capacity();
    }

    void copyIntoLane(float* base, std::size_t position, const float* src, std::size_t n) const noexcept {
        const std::size_t idx = position & cursor_.mask();
        const std::size_t first = std::min(n, cursor_.capacity() - idx);
        std::memcpy(base + idx, src, first * sizeof(float));
        std::memcpy(base, src + first, (n - first) * sizeof(float));
    }

    void copyFromLane(const float* base, std::size_t position, float* dst, std::size_t n) const noexcept {
        const std::size_t idx = position & cursor_.mask();
        const std::size_t first = std::min(n, cursor_.capacity() - idx);
        std::memcpy(dst, base + idx, first * sizeof(float));
        std::memcpy(dst + first, base, (n - first) * sizeof(float));
    }

    void zeroLane(float* base, std::size_t position, std::size_t n) const noexcept {
        const std::size_t idx = position & cursor_.mask();
        const std::size_t first = std::min(n, cursor_.capacity() - idx);
        std::memset(base + idx, 0, first * sizeof(float));
        std::memset(base, 0, (n - first) * sizeof(float));
    }

    // Each counter has exactly one writer, so load+store avoids a locked RMW.
    static void countDropped(std::atomic<std::uint64_t>& counter, std::size_t frames) noexcept {
        if (frames != 0) {
            counter.store(counter.load(std::memory_order_relaxed) + frames, std::memory_order_relaxed);
        }
    }

    detail::SpscCursor cursor_;
    const std::uint32_t numChannels_;
    alignas(kCacheLineSize) std::atomic<std::uint64_t> overruns_{0};   // producer-owned
    alignas(kCacheLineSize) std::atomic<std::uint64_t> underruns_{0};  // consumer-owned
};

} // namespace afm
