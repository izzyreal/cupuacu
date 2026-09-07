#pragma once

#include "../storage/AudioReader.hpp"
#include <memory>

namespace cupuacu::storage
{
    class ImportAudioReader;
}

namespace cupuacu::waveform
{
    class ProgressivePeaks;
    class SourcePeaks;

    // Import notifications share readers; no sample or peak arrays cross
    // the publication queue. Readers expose their currently available prefix.
    struct ImportPreview
    {
        storage::AudioShape shape;
        std::shared_ptr<const storage::ImportAudioReader> audio;
        std::shared_ptr<const ProgressivePeaks> progressivePeaks;
        std::shared_ptr<const SourcePeaks> sourcePeaks;
    };
}
