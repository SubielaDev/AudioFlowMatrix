#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <string>
#include <vector>

namespace afm::midi {

// Computer-keyboard shortcuts for the same targets as the MIDI map (push-to-talk, channel
// mutes). Keys are Windows virtual-key codes; the modifiers are told apart by side
// (VK_LCONTROL / VK_RCONTROL, ...), so "Right Ctrl" alone can be a push-to-talk key while
// "Left Ctrl + M" is a shortcut. Header-only and portable: the constants below are the
// Windows values, so the tests need no windows.h.
namespace vk {
constexpr std::uint8_t kEscape = 0x1B;
constexpr std::uint8_t kLWin = 0x5B, kRWin = 0x5C;
constexpr std::uint8_t kLShift = 0xA0, kRShift = 0xA1;
constexpr std::uint8_t kLControl = 0xA2, kRControl = 0xA3;
constexpr std::uint8_t kLMenu = 0xA4, kRMenu = 0xA5;  // Alt
} // namespace vk

// Modifier set of a shortcut: either side counts ("Ctrl+M" = left or right Ctrl).
enum KeyModifier : std::uint8_t { kModCtrl = 1, kModShift = 2, kModAlt = 4, kModWin = 8 };

[[nodiscard]] constexpr std::uint8_t modifierBit(std::uint8_t key) noexcept {
    switch (key) {
    case vk::kLControl: case vk::kRControl: return kModCtrl;
    case vk::kLShift: case vk::kRShift: return kModShift;
    case vk::kLMenu: case vk::kRMenu: return kModAlt;
    case vk::kLWin: case vk::kRWin: return kModWin;
    default: return 0;
    }
}

// One key going down or up, as the keyboard thread reports it. `modifiers` are the ones
// held at that moment, not counting the key itself.
struct KeyEvent {
    enum class Kind : std::uint8_t { Press, Release };
    Kind kind = Kind::Press;
    std::uint8_t key = 0;
    std::uint8_t modifiers = 0;
};

// Turns the raw down/up stream into presses and releases: auto-repeat while a key is held
// does not count as another press (a held push-to-talk key, a mute that would flicker).
// Keyboard thread only.
class KeyTracker {
public:
    // false when nothing changed (auto-repeat, or the release of a key never seen down).
    bool update(std::uint8_t key, bool down, KeyEvent& out) noexcept {
        const bool wasDown = isDown(key);
        if (down == wasDown) return false;
        setDown(key, down);
        const std::uint8_t own = modifierBit(key);
        out = {down ? KeyEvent::Kind::Press : KeyEvent::Kind::Release, key,
               static_cast<std::uint8_t>(modifiers() & ~own)};
        return true;
    }

    // Keys can go up while another window has grabbed the input (a UAC prompt, the lock
    // screen): forget them so nothing stays "down".
    void reset() noexcept { std::fill(std::begin(down_), std::end(down_), std::uint64_t{0}); }

    [[nodiscard]] bool anyDown() const noexcept {
        return std::any_of(std::begin(down_), std::end(down_), [](std::uint64_t w) { return w != 0; });
    }
    [[nodiscard]] bool isDown(std::uint8_t key) const noexcept { return (down_[key >> 6] >> (key & 63)) & 1; }

    [[nodiscard]] std::uint8_t modifiers() const noexcept {
        std::uint8_t m = 0;
        for (std::uint8_t k : {vk::kLControl, vk::kRControl, vk::kLShift, vk::kRShift, vk::kLMenu, vk::kRMenu,
                               vk::kLWin, vk::kRWin}) {
            if (isDown(k)) m |= modifierBit(k);
        }
        return m;
    }

private:
    void setDown(std::uint8_t key, bool on) noexcept {
        const std::uint64_t bit = std::uint64_t{1} << (key & 63);
        down_[key >> 6] = on ? down_[key >> 6] | bit : down_[key >> 6] & ~bit;
    }

    std::uint64_t down_[4] = {};
};

struct KeyBinding {
    enum class Action : std::uint8_t { None, Press, Release };

    std::uint8_t key = 0;  // 0 = not mapped
    std::uint8_t modifiers = 0;

    [[nodiscard]] constexpr bool mapped() const noexcept { return key != 0; }

    // A press needs exactly the bound modifiers (Ctrl+M is not M); a release of the key
    // counts whatever is held, so letting go of Ctrl first never leaves push-to-talk open.
    [[nodiscard]] constexpr Action actionFor(const KeyEvent& e) const noexcept {
        if (!mapped() || e.key != key) return Action::None;
        if (e.kind == KeyEvent::Kind::Release) return Action::Release;
        return e.modifiers == modifiers ? Action::Press : Action::None;
    }

    friend constexpr bool operator==(const KeyBinding& a, const KeyBinding& b) noexcept {
        return a.key == b.key && a.modifiers == b.modifiers;
    }

    // Shared with the keyboard thread as one atomic integer.
    [[nodiscard]] constexpr std::uint32_t pack() const noexcept {
        return static_cast<std::uint32_t>(modifiers) << 8 | key;
    }
    [[nodiscard]] static constexpr KeyBinding unpack(std::uint32_t v) noexcept {
        return {static_cast<std::uint8_t>(v & 0xFF), static_cast<std::uint8_t>((v >> 8) & 0x0F)};
    }

    // settings.ini form: "key:<modifiers>:<virtual key>", e.g. "key:1:77" = Ctrl+M.
    [[nodiscard]] std::string serialize() const {
        return "key:" + std::to_string(modifiers) + ':' + std::to_string(key);
    }
    [[nodiscard]] static KeyBinding parse(const std::string& s) {
        unsigned mods = 0, key = 0;
        char tail = 0;
        if (s.compare(0, 4, "key:") != 0) return {};
        if (std::sscanf(s.c_str() + 4, "%u:%u%c", &mods, &key, &tail) != 2) return {};
        if (mods > 0x0F || key == 0 || key > 0xFE || key == vk::kEscape) return {};
        return {static_cast<std::uint8_t>(key), static_cast<std::uint8_t>(mods)};
    }
};

// Learn mode: the next key (with the modifiers held) becomes the binding. A modifier on its
// own is learned when it is let go without another key in between, so Ctrl+M can still be
// learned by pressing Ctrl first. Escape cancels. GUI thread, fed the tracker's events.
class KeyLearner {
public:
    enum class Result : std::uint8_t { Waiting, Learned, Cancelled };

    Result feed(const KeyEvent& e, KeyBinding& out) noexcept {
        if (e.kind == KeyEvent::Kind::Press) {
            if (e.key == vk::kEscape) return Result::Cancelled;
            if (modifierBit(e.key) != 0) {
                pending_ = {e.key, e.modifiers};
                return Result::Waiting;
            }
            pending_ = {};
            out = {e.key, e.modifiers};
            return Result::Learned;
        }
        if (pending_.mapped() && e.key == pending_.key) {
            out = pending_;
            pending_ = {};
            return Result::Learned;
        }
        pending_ = {};  // another modifier let go first: start over
        return Result::Waiting;
    }

    void reset() noexcept { pending_ = {}; }

private:
    KeyBinding pending_;
};

// Which key drives which target ("ptt", "mute.in:<name>", ... as in MidiMap). One key per
// target and one target per key: assigning either side replaces the old pairing. GUI thread.
class KeyMap {
public:
    struct Entry {
        std::string target;
        KeyBinding binding;
    };

    void assign(const std::string& target, const KeyBinding& binding) {
        clear(target);
        if (!binding.mapped()) return;
        entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
                                      [&](const Entry& e) { return e.binding == binding; }),
                       entries_.end());
        entries_.push_back({target, binding});
    }

    void clear(const std::string& target) {
        entries_.erase(std::remove_if(entries_.begin(), entries_.end(), [&](const Entry& e) { return e.target == target; }),
                       entries_.end());
    }

    [[nodiscard]] KeyBinding bindingFor(const std::string& target) const {
        for (const Entry& e : entries_) {
            if (e.target == target) return e.binding;
        }
        return {};
    }

    [[nodiscard]] const std::string* targetFor(const KeyBinding& binding) const {
        for (const Entry& e : entries_) {
            if (e.binding == binding) return &e.target;
        }
        return nullptr;
    }

    [[nodiscard]] const std::vector<Entry>& entries() const noexcept { return entries_; }

    // settings.ini value: "<binding>|<target>", e.g. "key:0:163|ptt".
    static std::string serialize(const Entry& e) { return e.binding.serialize() + '|' + e.target; }
    void load(const std::string& line) {
        const auto bar = line.find('|');
        if (bar == std::string::npos || bar + 1 >= line.size()) return;
        const KeyBinding binding = KeyBinding::parse(line.substr(0, bar));
        if (binding.mapped()) assign(line.substr(bar + 1), binding);
    }

private:
    std::vector<Entry> entries_;
};

} // namespace afm::midi
