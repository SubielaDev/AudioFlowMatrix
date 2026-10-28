#pragma once

#include "afm/core/Config.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <type_traits>

namespace afm {

// Lock-free, wait-free "latest value wins" exchange between one writer and one reader.
// The writer fills back(), then publish(); the reader calls update() and, if it returns
// true, front() holds the newest complete snapshot. Neither side ever waits and the
// reader never sees a half-written value. Used for GUI -> audio parameter snapshots.
template <typename T>
class TripleBuffer {
    static_assert(std::is_trivially_copyable_v<T>);

public:
    TripleBuffer() = default;
    TripleBuffer(const TripleBuffer&) = delete;
    TripleBuffer& operator=(const TripleBuffer&) = delete;

    // ---- Writer thread --------------------------------------------------------------
    [[nodiscard]] T& back() noexcept { return slots_[writeIndex_].value; }

    void publish() noexcept {
        const std::uint8_t previous = middle_.exchange(static_cast<std::uint8_t>(writeIndex_ | kDirty),
                                                       std::memory_order_acq_rel);
        writeIndex_ = previous & kIndexMask;
    }

    // ---- Reader thread --------------------------------------------------------------
    // Returns true if a newer snapshot was published since the last call.
    [[nodiscard]] bool update() noexcept {
        if ((middle_.load(std::memory_order_relaxed) & kDirty) == 0) {
            return false;
        }
        const std::uint8_t previous = middle_.exchange(readIndex_, std::memory_order_acq_rel);
        readIndex_ = previous & kIndexMask;
        return true;
    }

    [[nodiscard]] const T& front() const noexcept { return slots_[readIndex_].value; }

private:
    static constexpr std::uint8_t kIndexMask = 0x3;
    static constexpr std::uint8_t kDirty = 0x4;

    struct alignas(kCacheLineSize) Slot {
        T value{};
    };

    std::array<Slot, 3> slots_{};
    alignas(kCacheLineSize) std::uint8_t writeIndex_ = 0;
    alignas(kCacheLineSize) std::uint8_t readIndex_ = 1;
    alignas(kCacheLineSize) std::atomic<std::uint8_t> middle_{2};
};

} // namespace afm
