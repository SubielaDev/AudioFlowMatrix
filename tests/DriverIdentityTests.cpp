// Tests for PeIdentity.h on the real virtual driver DLL. Exit code = number of failures.
//
//   afm_driver_identity_tests                 checks built-in mutations of this build's DLL
//   afm_driver_identity_tests a.dll b.dll     also checks that two given builds count as the same

#include "PeIdentity.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

#ifndef AFM_TEST_DRIVER_DLL
#error "AFM_TEST_DRIVER_DLL must name the built AudioFlowMatrixASIO.dll"
#endif

namespace {

int g_failures = 0;

#define AFM_CHECK(cond)                                                                   \
    do {                                                                                  \
        if (!(cond)) {                                                                    \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                                 \
        }                                                                                 \
    } while (0)

using afm::app::pe::sameImage;

std::vector<std::uint8_t> readFile(const char* path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::uint32_t u32(const std::vector<std::uint8_t>& b, std::size_t at) {
    std::uint32_t v = 0;
    std::memcpy(&v, b.data() + at, 4);
    return v;
}

bool same(const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b) {
    return sameImage(a.data(), a.size(), b.data(), b.size());
}

// Copy of `dll` with one byte changed.
std::vector<std::uint8_t> flipped(std::vector<std::uint8_t> dll, std::size_t at) {
    dll[at] ^= 0x5A;
    return dll;
}

void testMutations(const std::vector<std::uint8_t>& dll) {
    AFM_CHECK(dll.size() > 4096);
    if (dll.size() <= 4096) return;
    const std::size_t pe = u32(dll, 0x3C);
    AFM_CHECK(same(dll, dll));

    // Build-specific fields: still the same driver.
    AFM_CHECK(same(dll, flipped(dll, pe + 8)));  // COFF timestamp
    const char signature[] = "RSDS";
    const auto rsds = std::search(dll.begin(), dll.end(), signature, signature + 4);
    AFM_CHECK(rsds != dll.end());
    if (rsds != dll.end()) {
        const std::size_t at = static_cast<std::size_t>(rsds - dll.begin());
        AFM_CHECK(same(dll, flipped(dll, at + 4)));   // PDB GUID
        AFM_CHECK(same(dll, flipped(dll, at + 22)));  // PDB age
        AFM_CHECK(!same(dll, flipped(dll, at + 24))); // PDB file name is content
    }

    // Code: a different driver.
    const std::size_t optional = pe + 24;
    std::uint16_t optionalSize = 0;
    std::memcpy(&optionalSize, dll.data() + pe + 20, 2);
    const std::size_t firstSection = optional + optionalSize;
    const std::size_t codeStart = u32(dll, firstSection + 20);  // .text PointerToRawData
    AFM_CHECK(std::memcmp(dll.data() + firstSection, ".text", 5) == 0);
    AFM_CHECK(!same(dll, flipped(dll, codeStart + 100)));
    AFM_CHECK(!same(dll, flipped(dll, dll.size() / 2)));

    // Different sizes never match; non-PE data compares exactly.
    std::vector<std::uint8_t> longer = dll;
    longer.push_back(0);
    AFM_CHECK(!same(dll, longer));
    const std::vector<std::uint8_t> text = {'h', 'e', 'l', 'l', 'o'};
    std::vector<std::uint8_t> text2 = text;
    AFM_CHECK(same(text, text2));
    text2[0] = 'j';
    AFM_CHECK(!same(text, text2));
}

} // namespace

int main(int argc, char** argv) {
    testMutations(readFile(AFM_TEST_DRIVER_DLL));
    if (argc == 3) {
        const auto a = readFile(argv[1]);
        const auto b = readFile(argv[2]);
        AFM_CHECK(!a.empty() && !b.empty());
        std::printf("%s: %s\n", a == b ? "byte-identical" : "different bytes",
                    same(a, b) ? "same driver" : "different driver");
        AFM_CHECK(same(a, b));
    }
    if (g_failures == 0) std::printf("All driver identity tests passed.\n");
    return g_failures;
}
