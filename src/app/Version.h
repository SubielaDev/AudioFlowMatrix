#pragma once

// Release version handling for the update check. Pure functions, unit-tested
// (tests/UpdateTests.cpp).

#include <array>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>

namespace afm::app::version {

// "MAJOR.MINOR.PATCH", optionally prefixed with 'v'. Anything else (prerelease suffixes,
// missing parts, junk) is rejected rather than guessed at.
[[nodiscard]] inline std::optional<std::array<unsigned, 3>> parse(std::string_view s) {
    if (!s.empty() && (s.front() == 'v' || s.front() == 'V')) s.remove_prefix(1);
    std::array<unsigned, 3> parts{};
    for (std::size_t i = 0; i < 3; ++i) {
        if (s.empty() || !std::isdigit(static_cast<unsigned char>(s.front()))) return std::nullopt;
        unsigned value = 0;
        while (!s.empty() && std::isdigit(static_cast<unsigned char>(s.front()))) {
            value = value * 10 + static_cast<unsigned>(s.front() - '0');
            if (value > 1000000) return std::nullopt;
            s.remove_prefix(1);
        }
        parts[i] = value;
        if (i < 2) {
            if (s.empty() || s.front() != '.') return std::nullopt;
            s.remove_prefix(1);
        }
    }
    if (!s.empty()) return std::nullopt;
    return parts;
}

// True when `candidate` is a strictly newer valid version than `current`.
[[nodiscard]] inline bool isNewer(std::string_view candidate, std::string_view current) {
    const auto a = parse(candidate);
    const auto b = parse(current);
    return a && b && *a > *b;
}

// The "tag_name" of a GitHub "latest release" JSON response, or empty. Only a plain
// version-like tag is accepted: it ends up in a URL that is opened in the browser.
[[nodiscard]] inline std::string tagFromReleaseJson(std::string_view json) {
    constexpr std::string_view key = "\"tag_name\"";
    const auto k = json.find(key);
    if (k == std::string_view::npos) return {};
    auto i = k + key.size();
    while (i < json.size() && (json[i] == ' ' || json[i] == '\t' || json[i] == '\r' || json[i] == '\n')) ++i;
    if (i >= json.size() || json[i] != ':') return {};
    ++i;
    while (i < json.size() && (json[i] == ' ' || json[i] == '\t' || json[i] == '\r' || json[i] == '\n')) ++i;
    if (i >= json.size() || json[i] != '"') return {};
    const auto end = json.find('"', i + 1);
    if (end == std::string_view::npos || end - i - 1 > 32) return {};
    std::string tag(json.substr(i + 1, end - i - 1));
    return parse(tag) ? tag : std::string{};
}

} // namespace afm::app::version
