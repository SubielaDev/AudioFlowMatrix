// Tests for the shared-memory bridge between the app and the virtual ASIO driver, in
// particular that a corrupted or hostile mapping can never cause an out-of-bounds access.
// Exit code = number of failures.

#include "afm/asio/BridgeProtocol.h"

#include <windows.h>

#include <array>
#include <cstdio>
#include <limits>
#include <vector>

namespace {

int g_failures = 0;

#define AFM_CHECK(cond)                                                                   \
    do {                                                                                  \
        if (!(cond)) {                                                                    \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                                 \
        }                                                                                 \
    } while (0)

using namespace afm;
using namespace afm::bridge;

// The mapping, laid out so that its last byte is immediately followed by a no-access page:
// any read or write past kTotalBytes faults and fails the test run.
struct GuardedMapping {
    std::byte* region = nullptr;
    std::size_t pages = 0;
    void* base = nullptr;

    GuardedMapping() {
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        const std::size_t page = si.dwPageSize;
        pages = (kTotalBytes + page - 1) / page + 1;
        region = static_cast<std::byte*>(VirtualAlloc(nullptr, pages * page, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        DWORD old = 0;
        VirtualProtect(region + (pages - 1) * page, page, PAGE_NOACCESS, &old);
        base = region + (pages - 1) * page - kTotalBytes;  // ends exactly at the guard page
    }
    ~GuardedMapping() { VirtualFree(region, 0, MEM_RELEASE); }
};

struct Block {
    std::array<std::vector<float>, kChannels> lanes;
    std::array<float*, kChannels> ptrs{};
    explicit Block(std::uint32_t frames, float value = 0.0f) {
        for (std::uint32_t ch = 0; ch < kChannels; ++ch) {
            lanes[ch].assign(frames, value + static_cast<float>(ch));
            ptrs[ch] = lanes[ch].data();
        }
    }
};

void testRoundTripAndWrap() {
    GuardedMapping m;
    View v = construct(m.base);
    AFM_CHECK(isValid(m.base));
    // Cross the end of the ring several times, including the 64-bit counter wrap.
    auto* cursors = reinterpret_cast<RingCursors*>(static_cast<std::byte*>(m.base) + kToClientOffset);
    cursors->head.store(~std::uint64_t{0} - 100);
    cursors->tail.store(~std::uint64_t{0} - 100);
    bool ok = true;
    for (int i = 0; i < 40; ++i) {
        Block in(480, static_cast<float>(i));
        Block out(480, -1.0f);
        ok &= v.toClient.write(ConstAudioBlock{in.ptrs.data(), kChannels, 480}) == 480;
        ok &= v.toClient.read(AudioBlock{out.ptrs.data(), kChannels, 480}) == 480;
        ok &= out.lanes[3].front() == in.lanes[3].front() && out.lanes[7].back() == in.lanes[7].back();
    }
    AFM_CHECK(ok);
}

void testHostileCursors() {
    GuardedMapping m;
    View v = construct(m.base);
    auto* to = reinterpret_cast<RingCursors*>(static_cast<std::byte*>(m.base) + kToClientOffset);
    auto* from = reinterpret_cast<RingCursors*>(static_cast<std::byte*>(m.base) + kFromClientOffset);

    // Producer claims far more data than the ring holds: the consumer reads nothing,
    // resynchronises and outputs silence.
    to->head.store(123456789012345ull);
    to->tail.store(5);
    Block out(kMaxBlockFrames, 7.0f);
    AFM_CHECK(v.toClient.read(AudioBlock{out.ptrs.data(), kChannels, kMaxBlockFrames}) == 0);
    AFM_CHECK(out.lanes[0][0] == 0.0f && out.lanes[7][kMaxBlockFrames - 1] == 0.0f);
    AFM_CHECK(v.toClient.readableFrames() == 0);

    // Consumer cursor ahead of the producer: the producer writes nothing.
    from->head.store(10);
    from->tail.store(999999);
    Block in(kMaxBlockFrames, 1.0f);
    AFM_CHECK(v.fromClient.write(ConstAudioBlock{in.ptrs.data(), kChannels, kMaxBlockFrames}) == 0);

    // Arbitrary positions near the end of the address space of the counters: indices are
    // masked locally, so reads and writes stay inside the lanes.
    for (std::uint64_t pos : {~std::uint64_t{0}, std::uint64_t{1} << 63, std::uint64_t{kRingFrames} * 3 - 1}) {
        from->head.store(pos);
        from->tail.store(pos);
        AFM_CHECK(v.fromClient.write(ConstAudioBlock{in.ptrs.data(), kChannels, kMaxBlockFrames}) == kMaxBlockFrames);
        AFM_CHECK(v.fromClient.read(AudioBlock{out.ptrs.data(), kChannels, kMaxBlockFrames}) == kMaxBlockFrames);
        AFM_CHECK(out.lanes[5][kMaxBlockFrames - 1] == in.lanes[5][kMaxBlockFrames - 1]);
    }

    // Oversized requests are capped at one maximum block.
    std::vector<float> big(3 * kMaxBlockFrames, 2.0f);
    std::array<float*, kChannels> bigLanes{};
    bigLanes.fill(big.data());
    AFM_CHECK(v.fromClient.write(ConstAudioBlock{bigLanes.data(), kChannels, 3 * kMaxBlockFrames}) <= kMaxBlockFrames);

    // Fewer or more channels than the bridge has never index past its lanes.
    Block wide(64, 3.0f);
    AFM_CHECK(v.toClient.read(AudioBlock{wide.ptrs.data(), 2, 64}) <= 64);
}

void testHeaderValidation() {
    AFM_CHECK(validFormat(48000.0, 64));
    AFM_CHECK(validFormat(44100.0, kMaxBlockFrames));
    AFM_CHECK(!validFormat(48000.0, 0));
    AFM_CHECK(!validFormat(48000.0, kMaxBlockFrames + 1));
    AFM_CHECK(!validFormat(48000.0, 0xFFFFFFFFu));
    AFM_CHECK(!validFormat(0.0, 64));
    AFM_CHECK(!validFormat(-48000.0, 64));
    AFM_CHECK(!validFormat(1e12, 64));
    AFM_CHECK(!validFormat(std::numeric_limits<double>::quiet_NaN(), 64));

    GuardedMapping m;
    construct(m.base);
    static_cast<Header*>(m.base)->version = 1;  // an older driver/app on the other side
    AFM_CHECK(!isValid(m.base));
}

} // namespace

int main() {
    testRoundTripAndWrap();
    testHostileCursors();
    testHeaderValidation();
    if (g_failures == 0) std::printf("All bridge tests passed.\n");
    return g_failures;
}
