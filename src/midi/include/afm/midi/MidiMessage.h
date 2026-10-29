#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

namespace afm::midi {

// One decoded MIDI short message (as packed by WinMM: status | data1 << 8 | data2 << 16).
struct MidiEvent {
    enum class Kind : std::uint8_t { NoteOn, NoteOff, ControlChange, Other };

    Kind kind = Kind::Other;
    std::uint8_t channel = 0;  // 0..15
    std::uint8_t number = 0;   // note or controller number
    std::uint8_t value = 0;    // velocity or controller value
};

[[nodiscard]] constexpr MidiEvent parseShortMessage(std::uint32_t message) noexcept {
    const auto status = static_cast<std::uint8_t>(message & 0xFF);
    MidiEvent e;
    e.channel = status & 0x0F;
    e.number = static_cast<std::uint8_t>((message >> 8) & 0x7F);
    e.value = static_cast<std::uint8_t>((message >> 16) & 0x7F);
    switch (status & 0xF0) {
    case 0x90: e.kind = e.value == 0 ? MidiEvent::Kind::NoteOff : MidiEvent::Kind::NoteOn; break;  // vel 0 = off
    case 0x80: e.kind = MidiEvent::Kind::NoteOff; break;
    case 0xB0: e.kind = MidiEvent::Kind::ControlChange; break;
    default: e.kind = MidiEvent::Kind::Other; break;
    }
    return e;
}

// A control (pad, key or button) a function is mapped to.
struct MidiBinding {
    enum class Type : std::uint8_t { None, Note, ControlChange };
    enum class Action : std::uint8_t { None, Press, Release };

    // CC buttons send 127 when pressed and 0 when released; anything from here up counts as down.
    static constexpr std::uint8_t kCcPressThreshold = 64;

    Type type = Type::None;
    std::uint8_t channel = 0;
    std::uint8_t number = 0;

    // MIDI Learn: binds to the control that sent `e` (Note On or any CC), else None.
    [[nodiscard]] static constexpr MidiBinding learn(const MidiEvent& e) noexcept {
        if (e.kind == MidiEvent::Kind::NoteOn) return {Type::Note, e.channel, e.number};
        if (e.kind == MidiEvent::Kind::ControlChange) return {Type::ControlChange, e.channel, e.number};
        return {};
    }

    [[nodiscard]] constexpr Action actionFor(const MidiEvent& e) const noexcept {
        if (type == Type::None || e.channel != channel || e.number != number) return Action::None;
        if (type == Type::Note) {
            if (e.kind == MidiEvent::Kind::NoteOn) return Action::Press;
            if (e.kind == MidiEvent::Kind::NoteOff) return Action::Release;
            return Action::None;
        }
        if (e.kind != MidiEvent::Kind::ControlChange) return Action::None;
        return e.value >= kCcPressThreshold ? Action::Press : Action::Release;
    }

    // Lossless round trip through one integer, so threads can share it as an atomic.
    [[nodiscard]] constexpr std::uint32_t pack() const noexcept {
        return static_cast<std::uint32_t>(type) << 16 | static_cast<std::uint32_t>(channel) << 8 | number;
    }
    [[nodiscard]] static constexpr MidiBinding unpack(std::uint32_t v) noexcept {
        const auto t = static_cast<std::uint8_t>((v >> 16) & 0xFF);
        if (t > static_cast<std::uint8_t>(Type::ControlChange)) return {};
        return {static_cast<Type>(t), static_cast<std::uint8_t>((v >> 8) & 0x0F), static_cast<std::uint8_t>(v & 0x7F)};
    }

    // settings.ini form: "note:<channel 1-16>:<number>", "cc:<channel>:<number>" or "none".
    [[nodiscard]] std::string serialize() const {
        if (type == Type::None) return "none";
        return std::string(type == Type::Note ? "note:" : "cc:") + std::to_string(channel + 1) + ':' + std::to_string(number);
    }
    [[nodiscard]] static MidiBinding parse(const std::string& s) {
        unsigned ch = 0, num = 0;
        char kind[8] = {};
        if (std::sscanf(s.c_str(), "%7[a-z]:%u:%u", kind, &ch, &num) != 3 || ch < 1 || ch > 16 || num > 127) return {};
        const std::string k = kind;
        if (k != "note" && k != "cc") return {};
        return {k == "note" ? Type::Note : Type::ControlChange, static_cast<std::uint8_t>(ch - 1),
                static_cast<std::uint8_t>(num)};
    }

    // For the UI and the log, e.g. "Note 36 (ch 1)", "CC 64 (ch 10)", "not mapped".
    [[nodiscard]] std::string describe() const {
        if (type == Type::None) return "not mapped";
        return std::string(type == Type::Note ? "Note " : "CC ") + std::to_string(number) + " (ch " +
               std::to_string(channel + 1) + ")";
    }

    friend constexpr bool operator==(const MidiBinding& a, const MidiBinding& b) noexcept {
        return a.type == b.type && a.channel == b.channel && a.number == b.number;
    }
};

// For the UI and the log, e.g. "Note On   ch 1   #36  value 100   [90 24 64]".
[[nodiscard]] inline std::string describe(std::uint32_t message) {
    const MidiEvent e = parseShortMessage(message);
    const char* kind = e.kind == MidiEvent::Kind::NoteOn          ? "Note On "
                       : e.kind == MidiEvent::Kind::NoteOff       ? "Note Off"
                       : e.kind == MidiEvent::Kind::ControlChange ? "CC      "
                                                                  : "Other   ";
    char buf[96];
    std::snprintf(buf, sizeof(buf), "%s  ch %2u  #%3u  value %3u   [%02X %02X %02X]", kind, e.channel + 1u, e.number,
                  e.value, static_cast<unsigned>(message & 0xFF), static_cast<unsigned>((message >> 8) & 0xFF),
                  static_cast<unsigned>((message >> 16) & 0xFF));
    return buf;
}

} // namespace afm::midi
