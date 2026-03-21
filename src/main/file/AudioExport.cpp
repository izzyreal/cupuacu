#include "AudioExport.hpp"

#include <sndfile.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <initializer_list>
#include <optional>
#include <string_view>
#include <vector>

namespace
{
    using cupuacu::file::AudioExportCodec;
    using cupuacu::file::AudioExportContainer;
    using cupuacu::file::AudioExportEncoding;
    using cupuacu::file::AudioExportFormatOption;
    using cupuacu::file::AudioExportSettings;

    struct CandidateEncoding
    {
        const char *label;
        int subtype;
        const char *extension;
    };

    struct CandidateFormat
    {
        AudioExportContainer container;
        AudioExportCodec codec;
        const char *containerLabel;
        const char *codecLabel;
        int majorFormat;
        std::initializer_list<CandidateEncoding> encodings;
    };

    constexpr CandidateFormat kCandidateFormats[] = {
        {AudioExportContainer::WAV,
         AudioExportCodec::PCM,
         "WAV",
         "PCM",
         SF_FORMAT_WAV,
         {{"16-bit PCM", SF_FORMAT_PCM_16, "wav"},
          {"24-bit PCM", SF_FORMAT_PCM_24, "wav"},
          {"32-bit PCM", SF_FORMAT_PCM_32, "wav"},
          {"32-bit float", SF_FORMAT_FLOAT, "wav"}}},
        {AudioExportContainer::AIFF,
         AudioExportCodec::PCM,
         "AIFF",
         "PCM",
         SF_FORMAT_AIFF,
         {{"16-bit PCM", SF_FORMAT_PCM_16, "aiff"},
          {"24-bit PCM", SF_FORMAT_PCM_24, "aiff"},
          {"32-bit PCM", SF_FORMAT_PCM_32, "aiff"}}},
        {AudioExportContainer::CAF,
         AudioExportCodec::ALAC,
         "CAF",
         "ALAC",
         SF_FORMAT_CAF,
         {{"16-bit ALAC", SF_FORMAT_ALAC_16, "caf"},
          {"24-bit ALAC", SF_FORMAT_ALAC_24, "caf"},
          {"32-bit ALAC", SF_FORMAT_ALAC_32, "caf"}}},
        {AudioExportContainer::FLAC,
         AudioExportCodec::FLAC,
         "FLAC",
         "FLAC",
         SF_FORMAT_FLAC,
         {{"16-bit FLAC", SF_FORMAT_PCM_16, "flac"},
          {"24-bit FLAC", SF_FORMAT_PCM_24, "flac"}}},
        {AudioExportContainer::OGG,
         AudioExportCodec::VORBIS,
         "OGG",
         "Vorbis",
         SF_FORMAT_OGG,
         {{"Default quality", SF_FORMAT_VORBIS, "ogg"}}},
        {AudioExportContainer::MPEG,
         AudioExportCodec::MP3,
         "MPEG",
         "MP3",
         SF_FORMAT_MPEG,
         {{"Default quality", SF_FORMAT_MPEG_LAYER_III, "mp3"}}},
    };

    bool isFormatWritable(const int combinedFormat)
    {
        SF_INFO info{};
        info.frames = 0;
        info.channels = 2;
        info.samplerate = 44100;
        info.format = combinedFormat;
        return sf_format_check(&info) != 0;
    }

    std::optional<AudioExportSettings>
    makeSettings(const AudioExportFormatOption &option,
                 const AudioExportEncoding &encoding)
    {
        AudioExportSettings settings{};
        settings.container = option.container;
        settings.codec = option.codec;
        settings.majorFormat = option.majorFormat;
        settings.subtype = encoding.subtype;
        settings.containerLabel = option.containerLabel;
        settings.codecLabel = option.codecLabel;
        settings.encodingLabel = encoding.label;
        settings.extension = encoding.extension;
        return settings.isValid() ? std::optional<AudioExportSettings>(settings)
                                  : std::nullopt;
    }

    bool extensionEquals(const std::filesystem::path &path,
                         const std::string_view expected)
    {
        std::string actual = path.extension().string();
        if (!actual.empty() && actual.front() == '.')
        {
            actual.erase(actual.begin());
        }
        if (actual.size() != expected.size())
        {
            return false;
        }
        for (std::size_t i = 0; i < actual.size(); ++i)
        {
            if (std::tolower(static_cast<unsigned char>(actual[i])) !=
                std::tolower(static_cast<unsigned char>(expected[i])))
            {
                return false;
            }
        }
        return true;
    }

    int preferredSubtypeForDocument(const AudioExportFormatOption &option,
                                    const cupuacu::SampleFormat documentFormat)
    {
        switch (option.codec)
        {
            case AudioExportCodec::PCM:
                switch (documentFormat)
                {
                    case cupuacu::SampleFormat::PCM_S24:
                        return SF_FORMAT_PCM_24;
                    case cupuacu::SampleFormat::PCM_S32:
                        return SF_FORMAT_PCM_32;
                    case cupuacu::SampleFormat::FLOAT32:
                    case cupuacu::SampleFormat::FLOAT64:
                        return option.majorFormat == SF_FORMAT_WAV
                                   ? SF_FORMAT_FLOAT
                                   : SF_FORMAT_PCM_24;
                    case cupuacu::SampleFormat::PCM_S8:
                    case cupuacu::SampleFormat::PCM_S16:
                    case cupuacu::SampleFormat::Unknown:
                    default:
                        return SF_FORMAT_PCM_16;
                }
            case AudioExportCodec::ALAC:
                switch (documentFormat)
                {
                    case cupuacu::SampleFormat::PCM_S24:
                    case cupuacu::SampleFormat::PCM_S32:
                    case cupuacu::SampleFormat::FLOAT32:
                    case cupuacu::SampleFormat::FLOAT64:
                        return SF_FORMAT_ALAC_24;
                    case cupuacu::SampleFormat::PCM_S8:
                    case cupuacu::SampleFormat::PCM_S16:
                    case cupuacu::SampleFormat::Unknown:
                    default:
                        return SF_FORMAT_ALAC_16;
                }
            case AudioExportCodec::FLAC:
                switch (documentFormat)
                {
                    case cupuacu::SampleFormat::PCM_S24:
                    case cupuacu::SampleFormat::PCM_S32:
                    case cupuacu::SampleFormat::FLOAT32:
                    case cupuacu::SampleFormat::FLOAT64:
                        return SF_FORMAT_PCM_24;
                    case cupuacu::SampleFormat::PCM_S8:
                    case cupuacu::SampleFormat::PCM_S16:
                    case cupuacu::SampleFormat::Unknown:
                    default:
                        return SF_FORMAT_PCM_16;
                }
            case AudioExportCodec::VORBIS:
                return SF_FORMAT_VORBIS;
            case AudioExportCodec::MP3:
                return SF_FORMAT_MPEG_LAYER_III;
        }

        return 0;
    }

    const AudioExportEncoding *
    findEncoding(const AudioExportFormatOption &option, const int subtype)
    {
        for (const auto &encoding : option.encodings)
        {
            if (encoding.subtype == subtype)
            {
                return &encoding;
            }
        }
        return nullptr;
    }
} // namespace

std::vector<AudioExportFormatOption> cupuacu::file::probeAvailableExportFormats()
{
    std::vector<AudioExportFormatOption> result;

    for (const auto &candidate : kCandidateFormats)
    {
        AudioExportFormatOption option{};
        option.container = candidate.container;
        option.codec = candidate.codec;
        option.containerLabel = candidate.containerLabel;
        option.codecLabel = candidate.codecLabel;
        option.majorFormat = candidate.majorFormat;

        for (const auto &encoding : candidate.encodings)
        {
            if (isFormatWritable(candidate.majorFormat | encoding.subtype))
            {
                option.encodings.push_back(AudioExportEncoding{
                    encoding.label, encoding.subtype, encoding.extension});
            }
        }

        if (!option.encodings.empty())
        {
            result.push_back(std::move(option));
        }
    }

    return result;
}

std::optional<AudioExportSettings>
cupuacu::file::defaultExportSettingsForPath(
    const std::filesystem::path &outputPath,
    const cupuacu::SampleFormat documentFormat)
{
    const auto formats = probeAvailableExportFormats();

    auto chooseSettings =
        [&](const AudioExportFormatOption &option) -> std::optional<AudioExportSettings>
    {
        const int preferredSubtype =
            preferredSubtypeForDocument(option, documentFormat);
        if (const auto *preferred = findEncoding(option, preferredSubtype))
        {
            return makeSettings(option, *preferred);
        }
        if (!option.encodings.empty())
        {
            return makeSettings(option, option.encodings.front());
        }
        return std::nullopt;
    };

    for (const auto &format : formats)
    {
        for (const auto &encoding : format.encodings)
        {
            if (extensionEquals(outputPath, encoding.extension))
            {
                return chooseSettings(format);
            }
        }
    }

    for (const auto &format : formats)
    {
        if (format.container == AudioExportContainer::WAV &&
            format.codec == AudioExportCodec::PCM)
        {
            return chooseSettings(format);
        }
    }

    if (!formats.empty())
    {
        return chooseSettings(formats.front());
    }

    return std::nullopt;
}

std::optional<AudioExportSettings>
cupuacu::file::inferExportSettingsForFile(const std::filesystem::path &path,
                                          const int sndfileFormat,
                                          const cupuacu::SampleFormat documentFormat)
{
    const int major = sndfileFormat & SF_FORMAT_TYPEMASK;
    const int subtype = sndfileFormat & SF_FORMAT_SUBMASK;

    for (const auto &format : probeAvailableExportFormats())
    {
        if (format.majorFormat != major)
        {
            continue;
        }

        if (const auto *encoding = findEncoding(format, subtype))
        {
            return makeSettings(format, *encoding);
        }
    }

    return defaultExportSettingsForPath(path, documentFormat);
}

std::filesystem::path
cupuacu::file::normalizeExportPath(const std::filesystem::path &path,
                                   const AudioExportSettings &settings)
{
    if (!settings.isValid())
    {
        return path;
    }

    if (extensionEquals(path, settings.extension))
    {
        return path;
    }

    auto normalized = path;
    normalized.replace_extension("." + settings.extension);
    return normalized;
}

bool cupuacu::file::isOverwritePreservingWavRewriteCandidate(
    const AudioExportSettings &settings)
{
    return settings.majorFormat == SF_FORMAT_WAV &&
           settings.subtype == SF_FORMAT_PCM_16;
}
