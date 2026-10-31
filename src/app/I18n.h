#pragma once

// UI translations. Texts are written in English in the code and looked up with tr(); the
// English text is the key, so a missing translation shows the English one. Diagnostics (the
// log, error details from Windows or the drivers) stay in English.

#include <span>
#include <string_view>

namespace afm::app::i18n {

enum class Language { English, Spanish, Portuguese, French, German, Italian, Russian };

inline constexpr int kLanguageCount = 7;

struct LanguageInfo {
    Language language;
    const char* code;        // settings value, e.g. "es"
    const char* nativeName;  // shown in the language menu, e.g. "Español"
};

// One UI text in every language, in the order of Language.
struct Entry {
    const char* text[kLanguageCount];
};

[[nodiscard]] std::span<const LanguageInfo> languages();
[[nodiscard]] std::span<const Entry> entries();

[[nodiscard]] Language fromCode(std::string_view code, Language fallback);
[[nodiscard]] const char* code(Language language);
// The Windows display language when it is one of ours, else English.
[[nodiscard]] Language systemLanguage();

void setLanguage(Language language);
[[nodiscard]] Language language();

// Translated `text`. An ImGui ID suffix ("##id" or "###id") is kept as it is, so a widget
// keeps its identity when the language changes if it uses "###". The returned pointer
// stays valid until the language changes.
[[nodiscard]] const char* tr(const char* text);

} // namespace afm::app::i18n

namespace afm::app {
using i18n::tr;
}
