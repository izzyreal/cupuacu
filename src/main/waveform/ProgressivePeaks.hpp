#pragma once
#include "SourcePeaks.hpp"

namespace cupuacu::waveform
{
    // A single import worker appends; viewport workers read published prefixes.
    // Detailed tiles use the shared decoded cache; only active tiles and the
    // small top of the pyramid remain resident. No UI-thread disk access.
    class ProgressivePeaks
        : public std::enable_shared_from_this<ProgressivePeaks>
    {
        struct Impl;
        std::unique_ptr<Impl> impl;

    public:
        ProgressivePeaks(storage::AudioShape,
                         std::shared_ptr<storage::DecodedBlockCache>);
        ~ProgressivePeaks();
        void append(int channel, std::span<const Peak>);
        void publish(int64_t frames);
        int64_t availableFrames() const;
        Peak queryBlocks(int channel, int64_t first, int64_t end) const;
        std::shared_ptr<const SourcePeaks> finish();
        std::array<uint64_t, 3> residency() const;
    };
} // namespace cupuacu::waveform
