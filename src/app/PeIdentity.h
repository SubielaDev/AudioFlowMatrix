#pragma once

// Compares Windows DLLs by what they contain, not by when they were built.
//
// Every link writes a timestamp into the PE header and the debug directory, and a fresh
// GUID/age for its PDB, so two builds of identical driver code differ in about 20 bytes.
// The app compares the embedded driver with the registered one to decide whether to offer
// "Update driver"; comparing raw bytes made every new build ask, even when the driver had not
// changed. normalizePe() zeroes exactly those fields (and the optional header checksum);
// everything else, all code and data, must still match.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace afm::app::pe {

namespace detail {

inline bool read16(const std::vector<std::uint8_t>& b, std::size_t at, std::uint16_t& v) {
    if (at + 2 > b.size()) return false;
    std::memcpy(&v, b.data() + at, 2);
    return true;
}

inline bool read32(const std::vector<std::uint8_t>& b, std::size_t at, std::uint32_t& v) {
    if (at + 4 > b.size()) return false;
    std::memcpy(&v, b.data() + at, 4);
    return true;
}

inline void zero(std::vector<std::uint8_t>& b, std::size_t at, std::size_t n) {
    if (at <= b.size() && n <= b.size() - at) std::memset(b.data() + at, 0, n);
}

} // namespace detail

// A copy of the image with build timestamps, PDB identity and checksum cleared. Anything
// that does not parse as a PE image is returned unchanged, so it still compares exactly.
inline std::vector<std::uint8_t> normalizePe(const std::uint8_t* data, std::size_t size) {
    using namespace detail;
    std::vector<std::uint8_t> b(data, data + size);
    std::uint32_t pe = 0, signature = 0;
    if (size < 64 || b[0] != 'M' || b[1] != 'Z' || !read32(b, 0x3C, pe) || !read32(b, pe, signature) ||
        signature != 0x00004550) {
        return b;
    }
    std::uint16_t sections = 0, optionalSize = 0, magic = 0;
    if (!read16(b, pe + 6, sections) || !read16(b, pe + 20, optionalSize)) return b;
    const std::size_t optional = pe + 24;
    if (!read16(b, optional, magic) || (magic != 0x10B && magic != 0x20B)) return b;

    zero(b, pe + 8, 4);         // COFF TimeDateStamp
    zero(b, optional + 64, 4);  // CheckSum (same offset in PE32 and PE32+)

    // Debug directory: data directory entry 6.
    const std::size_t directories = optional + (magic == 0x20B ? 112 : 96);
    std::uint32_t debugRva = 0, debugSize = 0;
    if (!read32(b, directories + 6 * 8, debugRva) || !read32(b, directories + 6 * 8 + 4, debugSize) || debugRva == 0) {
        return b;
    }
    // RVA -> file offset through the section table.
    const std::size_t table = optional + optionalSize;
    std::size_t debugOffset = 0;
    for (std::uint16_t s = 0; s < sections; ++s) {
        std::uint32_t virtualSize = 0, virtualAddress = 0, rawSize = 0, rawPointer = 0;
        const std::size_t h = table + s * 40;
        if (!read32(b, h + 8, virtualSize) || !read32(b, h + 12, virtualAddress) || !read32(b, h + 16, rawSize) ||
            !read32(b, h + 20, rawPointer)) {
            return b;
        }
        const std::uint32_t span = virtualSize > rawSize ? virtualSize : rawSize;
        if (debugRva >= virtualAddress && debugRva - virtualAddress < span) {
            debugOffset = rawPointer + (debugRva - virtualAddress);
            break;
        }
    }
    if (debugOffset == 0) return b;
    for (std::uint32_t i = 0; i + 28 <= debugSize; i += 28) {
        const std::size_t entry = debugOffset + i;
        std::uint32_t type = 0, dataSize = 0, dataPointer = 0, rsds = 0;
        if (!read32(b, entry + 12, type) || !read32(b, entry + 16, dataSize) || !read32(b, entry + 24, dataPointer)) break;
        zero(b, entry + 4, 4);  // entry TimeDateStamp
        // CodeView "RSDS": signature, GUID (16 bytes), age (4 bytes), PDB path.
        if (type == 2 && dataSize >= 24 && read32(b, dataPointer, rsds) && rsds == 0x53445352) zero(b, dataPointer + 4, 20);
    }
    return b;
}

// True when the two images are the same build output apart from the fields above.
inline bool sameImage(const std::uint8_t* a, std::size_t aSize, const std::uint8_t* b, std::size_t bSize) {
    return aSize == bSize && normalizePe(a, aSize) == normalizePe(b, bSize);
}

} // namespace afm::app::pe
