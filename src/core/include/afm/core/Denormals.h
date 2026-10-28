#pragma once

#if defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__) || defined(__i386__)
#include <xmmintrin.h>
#define AFM_HAS_SSE_CSR 1
#endif

namespace afm {

// Sets flush-to-zero and denormals-are-zero for the current thread while in scope.
// Decaying feedback (filters, smoothed gains) otherwise produces denormals that can
// cost 100x per operation and blow the audio deadline. Put one at the top of every
// real-time callback.
class ScopedFlushDenormals {
public:
#if defined(AFM_HAS_SSE_CSR)
    ScopedFlushDenormals() noexcept : saved_(_mm_getcsr()) { _mm_setcsr(saved_ | kFtzDaz); }
    ~ScopedFlushDenormals() { _mm_setcsr(saved_); }
#else
    ScopedFlushDenormals() noexcept = default;
#endif

    ScopedFlushDenormals(const ScopedFlushDenormals&) = delete;
    ScopedFlushDenormals& operator=(const ScopedFlushDenormals&) = delete;

private:
#if defined(AFM_HAS_SSE_CSR)
    static constexpr unsigned int kFtzDaz = 0x8040u; // FTZ (bit 15) | DAZ (bit 6)
    unsigned int saved_;
#endif
};

} // namespace afm
