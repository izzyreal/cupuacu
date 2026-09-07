#pragma once

#include "AudioFileLoading.hpp"

namespace cupuacu { struct State; }
namespace cupuacu::file::legacy
{
    // Resident compatibility for format tests and explicit benchmark baselines.
    // Application opening and restoration must use owned audio import.
    using LoadChunkCallback = std::function<void(const Document &, int64_t)>;
    LoadedAudioFile loadAudioFile(
        const std::string &path, const LoadProgressCallback &progress = {},
        const LoadCancelCheck &isCanceled = {},
        const LoadChunkCallback &chunk = {});
    void loadSampleData(State *);
}
