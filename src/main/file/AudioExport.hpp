#pragma once

#include "../SampleFormat.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace cupuacu::file
{
    enum class AudioExportContainer
    {
        WAV,
        AIFF,
        CAF,
        FLAC,
        OGG,
        MPEG,
    };

    enum class AudioExportCodec
    {
        PCM,
        ALAC,
        FLAC,
        VORBIS,
        MP3,
    };

    struct AudioExportEncoding
    {
        std::string label;
        int subtype = 0;
        std::string extension;
    };

    struct AudioExportFormatOption
    {
        AudioExportContainer container = AudioExportContainer::WAV;
        AudioExportCodec codec = AudioExportCodec::PCM;
        std::string containerLabel;
        std::string codecLabel;
        int majorFormat = 0;
        std::vector<AudioExportEncoding> encodings;
    };

    struct AudioExportSettings
    {
        AudioExportContainer container = AudioExportContainer::WAV;
        AudioExportCodec codec = AudioExportCodec::PCM;
        int majorFormat = 0;
        int subtype = 0;
        std::string containerLabel;
        std::string codecLabel;
        std::string encodingLabel;
        std::string extension;

        bool isValid() const
        {
            return majorFormat != 0 && subtype != 0 && !extension.empty();
        }

        int sndfileFormat() const
        {
            return majorFormat | subtype;
        }
    };

    std::vector<AudioExportFormatOption> probeAvailableExportFormats();

    std::optional<AudioExportSettings>
    defaultExportSettingsForPath(const std::filesystem::path &outputPath,
                                 cupuacu::SampleFormat documentFormat);

    std::optional<AudioExportSettings>
    inferExportSettingsForFile(const std::filesystem::path &path,
                               int sndfileFormat,
                               cupuacu::SampleFormat documentFormat);

    std::filesystem::path
    normalizeExportPath(const std::filesystem::path &path,
                        const AudioExportSettings &settings);

    bool isOverwritePreservingWavRewriteCandidate(
        const AudioExportSettings &settings);
} // namespace cupuacu::file
