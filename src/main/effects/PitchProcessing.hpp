#pragma once

#include "EffectSettings.hpp"
#include "storage/AudioEditRevision.hpp"
#include "Document.hpp"
#include <filesystem>
#include <functional>

namespace cupuacu::effects
{
    constexpr double minPitchFactor = 1.0 / 16.0;
    constexpr double maxPitchFactor = 16.0;
    double pitchDisplayValue(const PitchSettings &);
    bool setPitchDisplayValue(PitchSettings &, const std::string &);
    int64_t pitchOutputFrames(int64_t inputFrames, const PitchSettings &);
    void remapPitchMarkers(std::span<DocumentMarker>, int64_t start,
                           int64_t inputFrames, int64_t outputFrames);

    // Worker-only. The result contains the target channels in request order.
    std::shared_ptr<const storage::AudioEditRevision>
    renderPitch(const storage::AudioReader &, int64_t start, int64_t frames,
                const std::vector<int64_t> &channels, const PitchSettings &,
                const std::filesystem::path &directory,
                const std::function<void(double)> &progress = {});
} // namespace cupuacu::effects
