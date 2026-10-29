#pragma once

#include "afm/midi/MidiMessage.h"

#include <algorithm>
#include <string>
#include <vector>

namespace afm::midi {

// Which MIDI control drives which app function. Targets are stable string ids:
//
//   "ptt"              push-to-talk
//   "mute.in:<name>"   MUTE of the input channel called <name>
//   "mute.out:<name>"  MUTE of the output channel called <name>
//
// Channels are keyed by name (like the routing matrix), so a mapping follows its channel
// across device changes and reordering. One control drives one target and one target has
// one control: assigning either side replaces the old pairing. GUI thread only.
class MidiMap {
public:
    struct Entry {
        std::string target;
        MidiBinding binding;
    };

    static std::string pttTarget() { return "ptt"; }
    static std::string inputMuteTarget(const std::string& channel) { return "mute.in:" + channel; }
    static std::string outputMuteTarget(const std::string& channel) { return "mute.out:" + channel; }

    void assign(const std::string& target, const MidiBinding& binding) {
        clear(target);
        if (binding.type == MidiBinding::Type::None) return;
        entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
                                      [&](const Entry& e) { return e.binding == binding; }),
                       entries_.end());
        entries_.push_back({target, binding});
    }

    void clear(const std::string& target) {
        entries_.erase(std::remove_if(entries_.begin(), entries_.end(), [&](const Entry& e) { return e.target == target; }),
                       entries_.end());
    }

    [[nodiscard]] MidiBinding bindingFor(const std::string& target) const {
        for (const Entry& e : entries_) {
            if (e.target == target) return e.binding;
        }
        return {};
    }

    // The target the control that sent `e` is mapped to, or nullptr.
    [[nodiscard]] const std::string* targetFor(const MidiEvent& e) const {
        for (const Entry& entry : entries_) {
            if (entry.binding.actionFor(e) != MidiBinding::Action::None) return &entry.target;
        }
        return nullptr;
    }

    [[nodiscard]] const std::vector<Entry>& entries() const noexcept { return entries_; }

    // settings.ini value: "<binding>|<target>", e.g. "note:1:36|mute.in:Mic L".
    static std::string serialize(const Entry& e) { return e.binding.serialize() + '|' + e.target; }
    // Adds one serialized entry; malformed lines are ignored.
    void load(const std::string& line) {
        const auto bar = line.find('|');
        if (bar == std::string::npos || bar + 1 >= line.size()) return;
        const MidiBinding binding = MidiBinding::parse(line.substr(0, bar));
        if (binding.type != MidiBinding::Type::None) assign(line.substr(bar + 1), binding);
    }

private:
    std::vector<Entry> entries_;
};

} // namespace afm::midi
