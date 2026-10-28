#pragma once

#include "afm/core/AlignedBuffer.h"
#include "afm/core/SpscCursor.h"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstring>
#include <span>
#include <type_traits>

namespace afm {

// Lock-free, wait-free bounded queue for exactly one producer thread and one consumer
// thread. Storage is one contiguous, pre-faulted, cache-line aligned block allocated in
// the constructor; every other member function is noexcept and allocation-free.
//
// Typical uses: telemetry frames (audio thread -> GUI), parameter changes
// (GUI -> audio thread), raw sample streams between device threads.
template <typename T>
class SpscRingBuffer {
    static_assert(std::is_trivially_copyable_v<T>, "SpscRingBuffer copies elements with memcpy");

public:
    // Two contiguous pieces of the ring, used by the zero-copy API. `second` is empty
    // unless the region wraps past the end of the storage.
    template <typename U>
    struct Regions {
        std::span<U> first;
        std::span<U> second;
        [[nodiscard]] std::size_t size() const noexcept { return first.size() + second.size(); }
    };

    // Not real-time safe: allocates. Capacity is rounded up to a power of two.
    explicit SpscRingBuffer(std::size_t minCapacity)
        : cursor_(roundCapacity(minCapacity)), storage_(cursor_.capacity()) {}

    SpscRingBuffer(const SpscRingBuffer&) = delete;
    SpscRingBuffer& operator=(const SpscRingBuffer&) = delete;

    [[nodiscard]] std::size_t capacity() const noexcept { return cursor_.capacity(); }
    [[nodiscard]] std::size_t sizeApprox() const noexcept { return cursor_.sizeApprox(); }

    // ---- Producer -------------------------------------------------------------------

    [[nodiscard]] std::size_t writable() noexcept { return cursor_.claimWrite(capacity()); }

    // Writes as many elements as fit; returns how many were written.
    std::size_t write(const T* src, std::size_t count) noexcept {
        const std::size_t n = cursor_.claimWrite(count);
        if (n != 0) {
            copyIn(cursor_.writeIndex(), src, n);
            cursor_.publishWrite(n);
        }
        return n;
    }

    // All-or-nothing write.
    [[nodiscard]] bool writeAll(const T* src, std::size_t count) noexcept {
        if (cursor_.claimWrite(count) < count) {
            return false;
        }
        copyIn(cursor_.writeIndex(), src, count);
        cursor_.publishWrite(count);
        return true;
    }

    [[nodiscard]] bool tryPush(const T& value) noexcept { return writeAll(&value, 1); }

    // Zero-copy: fill the returned regions in place, then commitWrite(<= regions.size()).
    [[nodiscard]] Regions<T> prepareWrite(std::size_t count) noexcept {
        const std::size_t n = cursor_.claimWrite(count);
        return regionsAt(cursor_.writeIndex(), n);
    }

    void commitWrite(std::size_t count) noexcept { cursor_.publishWrite(count); }

    // ---- Consumer -------------------------------------------------------------------

    [[nodiscard]] std::size_t readable() noexcept { return cursor_.claimRead(capacity()); }

    // Reads up to `count` elements; returns how many were read.
    std::size_t read(T* dst, std::size_t count) noexcept {
        const std::size_t n = cursor_.claimRead(count);
        if (n != 0) {
            copyOut(cursor_.readIndex(), dst, n);
            cursor_.publishRead(n);
        }
        return n;
    }

    // All-or-nothing read.
    [[nodiscard]] bool readAll(T* dst, std::size_t count) noexcept {
        if (cursor_.claimRead(count) < count) {
            return false;
        }
        copyOut(cursor_.readIndex(), dst, count);
        cursor_.publishRead(count);
        return true;
    }

    [[nodiscard]] bool tryPop(T& value) noexcept { return readAll(&value, 1); }

    // Drops up to `count` elements without copying them; returns how many were dropped.
    std::size_t discard(std::size_t count) noexcept {
        const std::size_t n = cursor_.claimRead(count);
        cursor_.publishRead(n);
        return n;
    }

    // Zero-copy: read the returned regions in place, then commitRead(<= regions.size()).
    [[nodiscard]] Regions<const T> prepareRead(std::size_t count) noexcept {
        const std::size_t n = cursor_.claimRead(count);
        const Regions<T> r = regionsAt(cursor_.readIndex(), n);
        return {r.first, r.second};
    }

    void commitRead(std::size_t count) noexcept { cursor_.publishRead(count); }

    // ---- Control --------------------------------------------------------------------

    // Not thread-safe: only call while neither side is running.
    void reset() noexcept { cursor_.reset(); }

private:
    static std::size_t roundCapacity(std::size_t n) noexcept { return std::bit_ceil(std::max<std::size_t>(n, 2)); }

    [[nodiscard]] Regions<T> regionsAt(std::size_t position, std::size_t n) noexcept {
        const std::size_t idx = position & cursor_.mask();
        const std::size_t first = std::min(n, capacity() - idx);
        return {std::span<T>(storage_.data() + idx, first), std::span<T>(storage_.data(), n - first)};
    }

    void copyIn(std::size_t position, const T* src, std::size_t n) noexcept {
        const Regions<T> r = regionsAt(position, n);
        std::memcpy(r.first.data(), src, r.first.size_bytes());
        if (!r.second.empty()) {
            std::memcpy(r.second.data(), src + r.first.size(), r.second.size_bytes());
        }
    }

    void copyOut(std::size_t position, T* dst, std::size_t n) noexcept {
        const Regions<T> r = regionsAt(position, n);
        std::memcpy(dst, r.first.data(), r.first.size_bytes());
        if (!r.second.empty()) {
            std::memcpy(dst + r.first.size(), r.second.data(), r.second.size_bytes());
        }
    }

    detail::SpscCursor cursor_;
    AlignedBuffer<T> storage_;
};

} // namespace afm
