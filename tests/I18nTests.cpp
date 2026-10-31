// Tests for the UI translations. Exit code = number of failures.

#include "I18n.h"

#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <string_view>
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

using namespace afm::app::i18n;

// The printf conversions of `format` in order, e.g. {"%s", "%.0f", "%%"}. A translation with
// different ones would read the wrong arguments.
std::vector<std::string> conversions(std::string_view format) {
    std::vector<std::string> out;
    for (std::size_t i = 0; i < format.size(); ++i) {
        if (format[i] != '%') continue;
        std::size_t j = i + 1;
        while (j < format.size() && std::strchr("-+ #0123456789.l", format[j]) != nullptr) ++j;
        out.emplace_back(format.substr(i, j + 1 - i));
        i = j;
    }
    return out;
}

// Code points from UTF-8; -1 for invalid UTF-8.
std::vector<long> codePoints(std::string_view s) {
    std::vector<long> out;
    for (std::size_t i = 0; i < s.size();) {
        const auto c = static_cast<unsigned char>(s[i]);
        const int extra = c < 0x80 ? 0 : (c >> 5) == 0x6 ? 1 : (c >> 4) == 0xE ? 2 : -1;
        if (extra < 0 || i + extra >= s.size() + (extra == 0)) return {-1};  // bad lead byte or truncated
        long cp = extra == 0 ? c : c & (extra == 1 ? 0x1F : 0x0F);
        for (int k = 1; k <= extra; ++k) {
            const auto cc = static_cast<unsigned char>(s[i + k]);
            if ((cc >> 6) != 0x2) return {-1};
            cp = (cp << 6) | (cc & 0x3F);
        }
        out.push_back(cp);
        i += extra + 1;
    }
    return out;
}

// What the UI font loads: Latin-1 and Cyrillic (main.cpp, GetGlyphRangesCyrillic).
bool fontHasGlyph(long cp) {
    return (cp >= 0x20 && cp <= 0xFF) || cp == '\n' || (cp >= 0x400 && cp <= 0x52F);
}

void testEntries() {
    std::set<std::string_view> keys;
    for (const Entry& e : entries()) {
        const std::string_view english = e.text[0];
        AFM_CHECK(keys.insert(english).second);  // no duplicate keys
        AFM_CHECK(english.find("##") == std::string_view::npos);
        const auto expected = conversions(english);
        for (int l = 0; l < kLanguageCount; ++l) {
            const char* text = e.text[l];
            AFM_CHECK(text != nullptr && *text != '\0');
            if (text == nullptr) continue;
            if (conversions(text) != expected) {
                std::fprintf(stderr, "  printf conversions differ: \"%s\" vs \"%s\"\n", e.text[0], text);
                ++g_failures;
            }
            for (long cp : codePoints(text)) {
                if (!fontHasGlyph(cp)) {
                    std::fprintf(stderr, "  character U+%04lX not in the UI font: \"%s\"\n", cp, text);
                    ++g_failures;
                }
            }
        }
    }
}

void testLookup() {
    setLanguage(Language::English);
    const char* stop = "Stop###start-stop";
    AFM_CHECK(tr(stop) == stop);  // English is the text itself
    setLanguage(Language::Spanish);
    AFM_CHECK(std::strcmp(tr(stop), "Detener###start-stop") == 0);  // ID suffix kept
    AFM_CHECK(std::strcmp(tr("Enable##mic"), "Activar##mic") == 0);
    AFM_CHECK(std::strcmp(tr("Not a known text"), "Not a known text") == 0);
    setLanguage(Language::Russian);
    AFM_CHECK(std::strcmp(tr("Settings"), "Настройки") == 0);
    setLanguage(Language::English);
}

void testCodes() {
    for (const LanguageInfo& l : languages()) {
        AFM_CHECK(fromCode(l.code, Language::English) == l.language);
        AFM_CHECK(std::string_view(code(l.language)) == l.code);
    }
    AFM_CHECK(fromCode("xx", Language::German) == Language::German);
    AFM_CHECK(fromCode("", Language::English) == Language::English);
}

} // namespace

int main() {
    testEntries();
    testLookup();
    testCodes();
    if (g_failures == 0) std::printf("All i18n tests passed (%zu texts).\n", entries().size());
    return g_failures;
}
