#pragma once

#include "afm/dsp/MatrixMixer.h"

#include <cstdint>
#include <iosfwd>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace afm {

inline constexpr float kFaderMinDb = -60.0f;  // at or below: -inf
inline constexpr float kFaderMaxDb = 12.0f;

struct ChannelStrip {
    float gainDb = 0.0f;
    bool mute = false;
};

struct Crosspoint {
    bool routed = false;
    float gainDb = 0.0f;
    bool mute = false;
    float pan = 0.0f;  // -1 (left) .. +1 (right); only acts on stereo-paired outputs
};

enum class PanSide : std::int8_t { Left = -1, Mono = 0, Right = 1 };

// User-facing routing state: input faders, output (bus) faders and a crosspoint matrix,
// all keyed by channel *name* so settings survive device changes and reordering.
// Lives on the GUI/control thread; compile() turns it into linear coefficients for the
// audio thread.
class RoutingModel {
public:
    // Defines the current bus layout. Settings for names seen before are kept.
    void setLayout(std::vector<std::string> inputs, std::vector<std::string> outputs, std::vector<PanSide> outputSides);

    [[nodiscard]] std::uint32_t numInputs() const noexcept { return static_cast<std::uint32_t>(inputNames_.size()); }
    [[nodiscard]] std::uint32_t numOutputs() const noexcept { return static_cast<std::uint32_t>(outputNames_.size()); }
    [[nodiscard]] const std::string& inputName(std::uint32_t i) const { return inputNames_[i]; }
    [[nodiscard]] const std::string& outputName(std::uint32_t o) const { return outputNames_[o]; }
    [[nodiscard]] PanSide outputSide(std::uint32_t o) const { return outputSides_[o]; }

    [[nodiscard]] ChannelStrip& input(std::uint32_t i) { return *inputStrips_[i]; }
    [[nodiscard]] ChannelStrip& output(std::uint32_t o) { return *outputStrips_[o]; }
    [[nodiscard]] Crosspoint& at(std::uint32_t out, std::uint32_t in) { return *dense_[out * inputNames_.size() + in]; }
    [[nodiscard]] const Crosspoint& at(std::uint32_t out, std::uint32_t in) const {
        return *dense_[out * inputNames_.size() + in];
    }

    // True if any crosspoint (in any layout seen so far) is routed.
    [[nodiscard]] bool hasRoutes() const;
    void clearRoutes();

    void compile(dsp::MixCoefficients& out) const;

    void save(std::ostream& os) const;
    void load(std::istream& is);

    [[nodiscard]] static float dbToLinear(float db) noexcept;

private:
    std::vector<std::string> inputNames_;
    std::vector<std::string> outputNames_;
    std::vector<PanSide> outputSides_;

    std::map<std::string, ChannelStrip> inputSettings_;
    std::map<std::string, ChannelStrip> outputSettings_;
    std::map<std::pair<std::string, std::string>, Crosspoint> crosspoints_;  // (input, output)

    // Pointers into the maps above for the current layout (map nodes are stable).
    std::vector<ChannelStrip*> inputStrips_;
    std::vector<ChannelStrip*> outputStrips_;
    std::vector<Crosspoint*> dense_;  // [out * numInputs + in]
};

} // namespace afm
