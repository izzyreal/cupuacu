#pragma once
#include "SourcePeaks.hpp"

namespace cupuacu::waveform
{
    class ProgressivePeaks;
    // Worker-only. Large sources spool only level-zero summaries, then build
    // bounded spatial tiles; small sources retain at most 4,096 base peaks.
    class StreamingPeakBuilder
    {
        struct Impl;
        std::unique_ptr<Impl> impl;

    public:
        using ReadAudio = std::function<void(int, int64_t, std::span<float>)>;
        StreamingPeakBuilder(
            storage::AudioShape,
            std::shared_ptr<storage::DecodedBlockCache> cache = {},
            std::function<bool()> cancel = {},
            std::shared_ptr<ProgressivePeaks> progressive = {});
        ~StreamingPeakBuilder();
        void appendFrom(storage::AudioShape, int64_t availableFrames,
                        const ReadAudio &);
        std::shared_ptr<const SourcePeaks> finish();
    };
} // namespace cupuacu::waveform
