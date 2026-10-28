#pragma once

#include "afm/core/Config.h"

#include <cstddef>
#include <cstring>
#include <new>
#include <type_traits>
#include <utility>

namespace afm {

// Owning, fixed-size, zero-initialised, over-aligned array.
//
// Allocation happens only in the constructor, so never construct one on a real-time
// thread. Zeroing the memory up front also commits every page, which keeps the audio
// thread from taking a soft page fault the first time it touches the buffer.
template <typename T, std::size_t Alignment = kCacheLineSize>
class AlignedBuffer {
    static_assert(std::is_trivially_copyable_v<T> && std::is_trivially_destructible_v<T>,
                  "AlignedBuffer only holds trivially copyable, trivially destructible types");
    static_assert((Alignment & (Alignment - 1)) == 0 && Alignment >= alignof(T),
                  "Alignment must be a power of two no smaller than alignof(T)");

public:
    AlignedBuffer() noexcept = default;

    explicit AlignedBuffer(std::size_t size) : size_(size) {
        if (size_ == 0) {
            return;
        }
        data_ = static_cast<T*>(::operator new(size_ * sizeof(T), std::align_val_t{Alignment}));
        std::memset(static_cast<void*>(data_), 0, size_ * sizeof(T));
    }

    ~AlignedBuffer() { release(); }

    AlignedBuffer(const AlignedBuffer&) = delete;
    AlignedBuffer& operator=(const AlignedBuffer&) = delete;

    AlignedBuffer(AlignedBuffer&& other) noexcept
        : data_(std::exchange(other.data_, nullptr)), size_(std::exchange(other.size_, 0)) {}

    AlignedBuffer& operator=(AlignedBuffer&& other) noexcept {
        if (this != &other) {
            release();
            data_ = std::exchange(other.data_, nullptr);
            size_ = std::exchange(other.size_, 0);
        }
        return *this;
    }

    [[nodiscard]] T* data() noexcept { return data_; }
    [[nodiscard]] const T* data() const noexcept { return data_; }
    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }

    [[nodiscard]] T& operator[](std::size_t i) noexcept { return data_[i]; }
    [[nodiscard]] const T& operator[](std::size_t i) const noexcept { return data_[i]; }

    void clear() noexcept {
        if (data_ != nullptr) {
            std::memset(static_cast<void*>(data_), 0, size_ * sizeof(T));
        }
    }

private:
    void release() noexcept {
        if (data_ != nullptr) {
            ::operator delete(data_, std::align_val_t{Alignment});
        }
        data_ = nullptr;
        size_ = 0;
    }

    T* data_ = nullptr;
    std::size_t size_ = 0;
};

} // namespace afm
