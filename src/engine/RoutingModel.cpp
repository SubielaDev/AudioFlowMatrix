#include "afm/engine/RoutingModel.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <istream>
#include <ostream>
#include <sstream>

namespace afm {

float RoutingModel::dbToLinear(float db) noexcept {
    return db <= kFaderMinDb ? 0.0f : std::pow(10.0f, db / 20.0f);
}

void RoutingModel::setLayout(std::vector<std::string> inputs, std::vector<std::string> outputs,
                             std::vector<PanSide> outputSides) {
    inputNames_ = std::move(inputs);
    outputNames_ = std::move(outputs);
    outputSides_ = std::move(outputSides);
    outputSides_.resize(outputNames_.size(), PanSide::Mono);

    inputStrips_.clear();
    outputStrips_.clear();
    dense_.clear();
    for (const auto& name : inputNames_) inputStrips_.push_back(&inputSettings_[name]);
    for (const auto& name : outputNames_) outputStrips_.push_back(&outputSettings_[name]);
    for (const auto& out : outputNames_) {
        for (const auto& in : inputNames_) dense_.push_back(&crosspoints_[{in, out}]);
    }
}

bool RoutingModel::hasRoutes() const {
    return std::any_of(crosspoints_.begin(), crosspoints_.end(), [](const auto& kv) { return kv.second.routed; });
}

void RoutingModel::clearRoutes() {
    for (auto& [key, xp] : crosspoints_) xp = Crosspoint{};
}

void RoutingModel::compile(dsp::MixCoefficients& c) const {
    c.numInputs = std::min<std::uint32_t>(numInputs(), kMaxInputs);
    c.numOutputs = std::min<std::uint32_t>(numOutputs(), kMaxOutputs);
    for (std::uint32_t o = 0; o < kMaxOutputs; ++o) {
        for (std::uint32_t i = 0; i < kMaxInputs; ++i) c.gain[o][i] = 0.0f;
    }
    for (std::uint32_t o = 0; o < c.numOutputs; ++o) {
        const ChannelStrip& out = *outputStrips_[o];
        if (out.mute) continue;
        const float outGain = dbToLinear(out.gainDb);
        for (std::uint32_t i = 0; i < c.numInputs; ++i) {
            const ChannelStrip& in = *inputStrips_[i];
            const Crosspoint& xp = at(o, i);
            if (!xp.routed || xp.mute || in.mute) continue;
            // Balance law: centre is unity on both sides, so straight routes are bit-exact.
            float pan = 1.0f;
            if (outputSides_[o] == PanSide::Left) pan = xp.pan > 0.0f ? 1.0f - xp.pan : 1.0f;
            if (outputSides_[o] == PanSide::Right) pan = xp.pan < 0.0f ? 1.0f + xp.pan : 1.0f;
            c.gain[o][i] = dbToLinear(xp.gainDb) * dbToLinear(in.gainDb) * outGain * pan;
        }
    }
}

// Line format, tab separated:
//   in   <name> <gainDb> <mute>
//   out  <name> <gainDb> <mute>
//   xp   <input> <output> <gainDb> <mute> <pan>
void RoutingModel::save(std::ostream& os) const {
    for (const auto& [name, s] : inputSettings_) {
        if (s.gainDb != 0.0f || s.mute) os << "in\t" << name << '\t' << s.gainDb << '\t' << s.mute << '\n';
    }
    for (const auto& [name, s] : outputSettings_) {
        if (s.gainDb != 0.0f || s.mute) os << "out\t" << name << '\t' << s.gainDb << '\t' << s.mute << '\n';
    }
    for (const auto& [key, xp] : crosspoints_) {
        if (xp.routed) {
            os << "xp\t" << key.first << '\t' << key.second << '\t' << xp.gainDb << '\t' << xp.mute << '\t' << xp.pan
               << '\n';
        }
    }
}

void RoutingModel::load(std::istream& is) {
    std::string line;
    while (std::getline(is, line)) {
        std::vector<std::string> f;
        std::stringstream ss(line);
        std::string field;
        while (std::getline(ss, field, '\t')) f.push_back(field);
        auto num = [](const std::string& s) { return std::strtof(s.c_str(), nullptr); };
        if ((f.size() == 4) && (f[0] == "in" || f[0] == "out")) {
            auto& map = f[0] == "in" ? inputSettings_ : outputSettings_;
            map[f[1]] = ChannelStrip{num(f[2]), f[3] == "1"};
        } else if (f.size() == 6 && f[0] == "xp") {
            crosspoints_[{f[1], f[2]}] = Crosspoint{true, num(f[3]), f[4] == "1", std::clamp(num(f[5]), -1.0f, 1.0f)};
        }
    }
    setLayout(inputNames_, outputNames_, outputSides_);
}

} // namespace afm
