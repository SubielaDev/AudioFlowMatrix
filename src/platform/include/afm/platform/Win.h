#pragma once

// Small Windows helpers shared by the app, the backends and the virtual driver DLL.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <string>
#include <string_view>
#include <utility>

namespace afm::win {

// Minimal COM smart pointer (avoids depending on WRL, which MinGW lacks).
template <typename T>
class ComPtr {
public:
    ComPtr() noexcept = default;
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;
    ComPtr(ComPtr&& o) noexcept : p_(std::exchange(o.p_, nullptr)) {}
    ComPtr& operator=(ComPtr&& o) noexcept {
        if (this != &o) {
            reset();
            p_ = std::exchange(o.p_, nullptr);
        }
        return *this;
    }
    ~ComPtr() { reset(); }

    void reset() noexcept {
        if (p_ != nullptr) {
            p_->Release();
            p_ = nullptr;
        }
    }

    [[nodiscard]] T* get() const noexcept { return p_; }
    T* operator->() const noexcept { return p_; }
    explicit operator bool() const noexcept { return p_ != nullptr; }

    // For out-parameters: releases the current pointer first.
    T** put() noexcept {
        reset();
        return &p_;
    }
    void** putVoid() noexcept { return reinterpret_cast<void**>(put()); }

private:
    T* p_ = nullptr;
};

inline std::string toUtf8(std::wstring_view w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

inline std::wstring toWide(std::string_view s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

inline std::string hresultMessage(HRESULT hr) {
    char buf[64];
    wsprintfA(buf, "HRESULT 0x%08lX", static_cast<unsigned long>(hr));
    return buf;
}

// Registers the calling thread with MMCSS ("Pro Audio") for the lifetime of the object.
// avrt.dll is loaded dynamically so the binaries have no hard dependency on it.
class ScopedMmcss {
public:
    explicit ScopedMmcss(const wchar_t* task = L"Pro Audio") noexcept {
        static const Api api = loadApi();
        api_ = &api;
        if (api.setCharacteristics != nullptr) {
            DWORD index = 0;
            handle_ = api.setCharacteristics(task, &index);
            if (handle_ != nullptr && api.setPriority != nullptr) {
                api.setPriority(handle_, 1 /* AVRT_PRIORITY_HIGH */);
            }
        }
        if (handle_ == nullptr) {
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
        }
    }

    ~ScopedMmcss() {
        if (handle_ != nullptr && api_->revert != nullptr) {
            api_->revert(handle_);
        }
    }

    ScopedMmcss(const ScopedMmcss&) = delete;
    ScopedMmcss& operator=(const ScopedMmcss&) = delete;

    [[nodiscard]] bool active() const noexcept { return handle_ != nullptr; }

private:
    struct Api {
        HANDLE(WINAPI* setCharacteristics)(LPCWSTR, LPDWORD) = nullptr;
        BOOL(WINAPI* setPriority)(HANDLE, int) = nullptr;
        BOOL(WINAPI* revert)(HANDLE) = nullptr;
    };

    static Api loadApi() noexcept {
        Api api;
        if (HMODULE m = LoadLibraryW(L"avrt.dll")) {
            api.setCharacteristics = reinterpret_cast<decltype(api.setCharacteristics)>(
                reinterpret_cast<void*>(GetProcAddress(m, "AvSetMmThreadCharacteristicsW")));
            api.setPriority = reinterpret_cast<decltype(api.setPriority)>(
                reinterpret_cast<void*>(GetProcAddress(m, "AvSetMmThreadPriority")));
            api.revert = reinterpret_cast<decltype(api.revert)>(
                reinterpret_cast<void*>(GetProcAddress(m, "AvRevertMmThreadCharacteristics")));
        }
        return api;
    }

    const Api* api_ = nullptr;
    HANDLE handle_ = nullptr;
};

// Monotonic nanoseconds from QueryPerformanceCounter.
inline unsigned long long nowNanoseconds() noexcept {
    static const long long freq = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return f.QuadPart;
    }();
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    const long long secs = c.QuadPart / freq;
    const long long rem = c.QuadPart % freq;
    return static_cast<unsigned long long>(secs) * 1'000'000'000ull +
           static_cast<unsigned long long>(rem * 1'000'000'000ll / freq);
}

} // namespace afm::win
