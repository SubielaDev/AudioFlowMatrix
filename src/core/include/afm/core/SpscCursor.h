#pragma once

#include "afm/core/Config.h"

#include <atomic>
#include <cstddef>

namespace afm::detail {

// Index bookkeeping shared by every single-producer / single-consumer ring.
//
// - Indices are free-running 64-bit counters; the slot is `index & mask`. Capacity
//   must be a power of two, so unsigned wrap-around of the counters is harmless.
// - Producer and consumer state live on separate cache lines. Each side also keeps a
//   private copy of the other side's index and only re-reads the shared atomic when
//   that cached value says there is not enough room/data. In steady state this means
//   one cross-core cache-line transfer per block instead of one per call.
// - Memory ordering: data is written, then `head` is published with release. The
//   consumer acquires `head`, reads the data, then publishes `tail` with release, which
//   the producer acquires before it overwrites those slots.
class SpscCursor {
public:
    explicit SpscCursor(std::size_t capacity) noexcept : capacity_(capacity), mask_(capacity - 1) {}

    SpscCursor(const SpscCursor&) = delete;
    SpscCursor& operator=(const SpscCursor&) = delete;

    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }
    [[nodiscard]] std::size_t mask() const noexcept { return mask_; }

    // ---- Producer thread only -------------------------------------------------------

    [[nodiscard]] std::size_t writeIndex() const noexcept {
        return producer_.head.load(std::memory_order_relaxed);
    }

    // Returns how many of `wanted` slots can be written right now (possibly 0).
    [[nodiscard]] std::size_t claimWrite(std::size_t wanted) noexcept {
        const std::size_t head = producer_.head.load(std::memory_order_relaxed);
        std::size_t free = capacity_ - (head - producer_.cachedTail);
        if (free < wanted) {
            producer_.cachedTail = consumer_.tail.load(std::memory_order_acquire);
            free = capacity_ - (head - producer_.cachedTail);
        }
        return wanted < free ? wanted : free;
    }

    // `count` must not exceed the last value returned by claimWrite().
    void publishWrite(std::size_t count) noexcept {
        const std::size_t head = producer_.head.load(std::memory_order_relaxed);
        producer_.head.store(head + count, std::memory_order_release);
    }

    // ---- Consumer thread only -------------------------------------------------------

    [[nodiscard]] std::size_t readIndex() const noexcept {
        return consumer_.tail.load(std::memory_order_relaxed);
    }

    [[nodiscard]] std::size_t claimRead(std::size_t wanted) noexcept {
        const std::size_t tail = consumer_.tail.load(std::memory_order_relaxed);
        std::size_t available = consumer_.cachedHead - tail;
        if (available < wanted) {
            consumer_.cachedHead = producer_.head.load(std::memory_order_acquire);
            available = consumer_.cachedHead - tail;
        }
        return wanted < available ? wanted : available;
    }

    void publishRead(std::size_t count) noexcept {
        const std::size_t tail = consumer_.tail.load(std::memory_order_relaxed);
        consumer_.tail.store(tail + count, std::memory_order_release);
    }

    // ---- Any thread -----------------------------------------------------------------

    // Snapshot of the fill level. Exact from the consumer thread, a lower bound of the
    // free space from the producer thread, approximate from anywhere else.
    [[nodiscard]] std::size_t sizeApprox() const noexcept {
        // Load tail first: head only moves forward, so the result can't go negative.
        const std::size_t tail = consumer_.tail.load(std::memory_order_acquire);
        const std::size_t head = producer_.head.load(std::memory_order_acquire);
        const std::size_t size = head - tail;
        return size < capacity_ ? size : capacity_;
    }

    // Not thread-safe: only call while neither side is running.
    void reset() noexcept {
        producer_.head.store(0, std::memory_order_relaxed);
        producer_.cachedTail = 0;
        consumer_.tail.store(0, std::memory_order_relaxed);
        consumer_.cachedHead = 0;
    }

private:
    static_assert(std::atomic<std::size_t>::is_always_lock_free);

    struct alignas(kCacheLineSize) ProducerState {
        std::atomic<std::size_t> head{0};
        std::size_t cachedTail = 0;
    };

    struct alignas(kCacheLineSize) ConsumerState {
        std::atomic<std::size_t> tail{0};
        std::size_t cachedHead = 0;
    };

    ProducerState producer_;
    ConsumerState consumer_;

    // Read-only after construction; kept off the two hot lines above.
    alignas(kCacheLineSize) const std::size_t capacity_;
    const std::size_t mask_;
};

} // namespace afm::detail
