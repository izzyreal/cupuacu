#pragma once
#include "BackgroundEffect.hpp"
#include "../../storage/AudioEditRevision.hpp"

namespace cupuacu::actions::effects
{
    // Worker-only. Scratch samples are bounded to two mono blocks regardless
    // of selection length/channel count. Outputs own their generated blocks.
    std::unique_ptr<BackgroundEffectResult> computeRevisionEffect(
        const BackgroundEffectRequest &request,
        std::shared_ptr<const storage::AudioEditRevision> source,
        const std::filesystem::path &workingDirectory,
        const std::function<void(const std::string &, std::optional<double>)>
            &progress);
} // namespace cupuacu::actions::effects
