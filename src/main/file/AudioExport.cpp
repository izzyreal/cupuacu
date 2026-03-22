#include "AudioExport.hpp"

#include <sndfile.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <optional>
#include <string_view>
#include <vector>

namespace
{
    using cupuacu::file::AudioExportCodec;
    using cupuacu::file::AudioExportContainer;
    using cupuacu::file::AudioExportEncoding;
    using cupuacu::file::AudioExportFormatOption;
    using cupuacu::file::AudioExportNamedDoubleOption;
    using cupuacu::file::AudioExportNamedIntOption;
    using cupuacu::file::AudioExportSettings;
    using cupuacu::file::AudioOpenFormatOption;

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

    struct CandidateOpenFormat
    {
        int majorFormat;
        const char *label;
        std::initializer_list<const char *> extensions;
    };

    constexpr CandidateOpenFormat kCandidateOpenFormats[] = {
        {SF_FORMAT_WAV, "WAV audio", {"wav"}},
        {SF_FORMAT_AIFF, "AIFF audio", {"aiff", "aif", "aifc"}},
        {SF_FORMAT_CAF, "CAF audio", {"caf"}},
        {SF_FORMAT_FLAC, "FLAC audio", {"flac"}},
        {SF_FORMAT_OGG, "OGG audio", {"ogg", "oga"}},
        {SF_FORMAT_MPEG, "MP3 audio", {"mp3"}},
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
        settings.compressionLevel =
            cupuacu::file::defaultCompressionLevelForCodec(option.codec);
        settings.bitrateMode =
            cupuacu::file::defaultBitrateModeForCodec(option.codec);
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

    struct MpegBitrateBand
    {
        int minSampleRate = 0;
        int maxSampleRateExclusive = 0;
        int minKbps = 0;
        int maxKbps = 0;
        std::initializer_list<int> bitrates;
    };

    constexpr MpegBitrateBand kMpegBitrateBands[] = {
        {32000, std::numeric_limits<int>::max(), 32, 320,
         {32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320}},
        {16000, 32000, 8, 160,
         {8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160}},
        {0, 16000, 8, 64,
         {8, 16, 24, 32, 40, 48, 56, 64}},
    };

    const MpegBitrateBand *mpegBitrateBandForSampleRate(const int sampleRate)
    {
        for (const auto &band : kMpegBitrateBands)
        {
            if (sampleRate >= band.minSampleRate &&
                sampleRate < band.maxSampleRateExclusive)
            {
                return &band;
            }
        }
        return nullptr;
    }

    std::optional<double> compressionLevelForMpegBitrate(const int sampleRate,
                                                         const int bitrateKbps)
    {
        const auto *band = mpegBitrateBandForSampleRate(sampleRate);
        if (band == nullptr || band->maxKbps <= band->minKbps)
        {
            return std::nullopt;
        }

        const int clampedBitrate =
            std::clamp(bitrateKbps, band->minKbps, band->maxKbps);
        const double normalized =
            1.0 - (static_cast<double>(clampedBitrate - band->minKbps) /
                   static_cast<double>(band->maxKbps - band->minKbps));
        return std::clamp(normalized, 0.0, 1.0);
    }

    std::optional<AudioExportSettings>
    makeSettings(const CandidateFormat &candidate, const int subtype)
    {
        AudioExportFormatOption option{};
        option.container = candidate.container;
        option.codec = candidate.codec;
        option.containerLabel = candidate.containerLabel;
        option.codecLabel = candidate.codecLabel;
        option.majorFormat = candidate.majorFormat;

        for (const auto &encoding : candidate.encodings)
        {
            if (encoding.subtype == subtype)
            {
                return makeSettings(
                    option,
                    AudioExportEncoding{encoding.label, encoding.subtype,
                                        encoding.extension});
            }
        }

        return std::nullopt;
    }

    std::optional<AudioExportSettings>
    fallbackSettingsForCandidate(const CandidateFormat &candidate,
                                 const cupuacu::SampleFormat documentFormat)
    {
        const int preferredSubtype =
            preferredSubtypeForDocument(AudioExportFormatOption{
                                            .container = candidate.container,
                                            .codec = candidate.codec,
                                            .containerLabel =
                                                candidate.containerLabel,
                                            .codecLabel = candidate.codecLabel,
                                            .majorFormat = candidate.majorFormat,
                                        },
                                        documentFormat);

        if (const auto preferred = makeSettings(candidate, preferredSubtype))
        {
            return preferred;
        }

        if (candidate.encodings.size() > 0)
        {
            return makeSettings(candidate, candidate.encodings.begin()->subtype);
        }

        return std::nullopt;
    }

    std::optional<AudioExportSettings>
    fallbackSettingsForPath(const std::filesystem::path &outputPath,
                            const cupuacu::SampleFormat documentFormat)
    {
        for (const auto &candidate : kCandidateFormats)
        {
            for (const auto &encoding : candidate.encodings)
            {
                if (extensionEquals(outputPath, encoding.extension))
                {
                    return fallbackSettingsForCandidate(candidate,
                                                       documentFormat);
                }
            }
        }

        for (const auto &candidate : kCandidateFormats)
        {
            if (candidate.container == AudioExportContainer::WAV &&
                candidate.codec == AudioExportCodec::PCM)
            {
                return fallbackSettingsForCandidate(candidate, documentFormat);
            }
        }

        return std::nullopt;
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

std::vector<AudioOpenFormatOption> cupuacu::file::probeAvailableOpenFormats()
{
    std::vector<AudioOpenFormatOption> result;
    result.reserve(std::size(kCandidateOpenFormats));

    for (const auto &candidate : kCandidateOpenFormats)
    {
        AudioOpenFormatOption option{};
        option.majorFormat = candidate.majorFormat;
        option.label = candidate.label;
        option.extensions.assign(candidate.extensions.begin(),
                                 candidate.extensions.end());
        result.push_back(std::move(option));
    }

    return result;
}

std::vector<AudioExportNamedDoubleOption>
cupuacu::file::compressionLevelOptionsForCodec(const AudioExportCodec codec)
{
    switch (codec)
    {
        case AudioExportCodec::VORBIS:
            return {{"Low", 0.25},
                    {"Medium", 0.5},
                    {"High", 0.7},
                    {"Very high", 0.9}};
        case AudioExportCodec::MP3:
            return {{"Lower quality", 0.95},
                    {"Balanced", 0.75},
                    {"High quality", 0.5},
                    {"Best quality", 0.2}};
        default:
            return {};
    }
}

std::vector<AudioExportNamedIntOption>
cupuacu::file::bitrateModeOptionsForCodec(const AudioExportCodec codec)
{
    switch (codec)
    {
        case AudioExportCodec::MP3:
            return {{"Constant bitrate", SF_BITRATE_MODE_CONSTANT},
                    {"Average bitrate", SF_BITRATE_MODE_AVERAGE},
                    {"Variable bitrate", SF_BITRATE_MODE_VARIABLE}};
        default:
            return {};
    }
}

std::vector<AudioExportNamedIntOption>
cupuacu::file::bitrateOptionsForSettings(const AudioExportSettings &settings,
                                         const int sampleRate)
{
    if (settings.codec != AudioExportCodec::MP3 ||
        !settings.bitrateMode.has_value() ||
        *settings.bitrateMode == SF_BITRATE_MODE_VARIABLE)
    {
        return {};
    }

    const auto *band = mpegBitrateBandForSampleRate(sampleRate);
    if (band == nullptr)
    {
        return {};
    }

    std::vector<AudioExportNamedIntOption> result;
    result.reserve(static_cast<std::size_t>(band->bitrates.size()));
    for (const int bitrate : band->bitrates)
    {
        result.push_back({std::to_string(bitrate) + " kbps", bitrate});
    }
    return result;
}

std::optional<double>
cupuacu::file::defaultCompressionLevelForCodec(const AudioExportCodec codec)
{
    switch (codec)
    {
        case AudioExportCodec::VORBIS:
            return 0.7;
        case AudioExportCodec::MP3:
            return 0.75;
        default:
            return std::nullopt;
    }
}

std::optional<int>
cupuacu::file::defaultBitrateModeForCodec(const AudioExportCodec codec)
{
    switch (codec)
    {
        case AudioExportCodec::MP3:
            return SF_BITRATE_MODE_VARIABLE;
        default:
            return std::nullopt;
    }
}

std::optional<int>
cupuacu::file::defaultBitrateKbpsForSettings(const AudioExportSettings &settings,
                                             const int sampleRate)
{
    const auto options = bitrateOptionsForSettings(settings, sampleRate);
    if (options.empty())
    {
        return std::nullopt;
    }

    if (settings.codec == AudioExportCodec::MP3)
    {
        const auto hasBitrate = [&](const int bitrate)
        {
            return std::any_of(options.begin(), options.end(),
                               [bitrate](const AudioExportNamedIntOption &option)
                               {
                                   return option.value == bitrate;
                               });
        };
        if (hasBitrate(192))
        {
            return 192;
        }
    }

    return options.front().value;
}

std::string
cupuacu::file::describeExportSettings(const AudioExportSettings &settings)
{
    std::string description = "Export to ." + settings.extension + " using " +
                              settings.containerLabel + " / " +
                              settings.codecLabel + " / " +
                              settings.encodingLabel;

    if (settings.codec == AudioExportCodec::VORBIS &&
        settings.compressionLevel.has_value())
    {
        const int percent = static_cast<int>(
            std::lround(*settings.compressionLevel * 100.0));
        description += " / quality " + std::to_string(percent) + "%";
    }
    else if (settings.codec == AudioExportCodec::MP3)
    {
        if (settings.bitrateMode.has_value())
        {
            switch (*settings.bitrateMode)
            {
                case SF_BITRATE_MODE_CONSTANT:
                    description += " / CBR";
                    break;
                case SF_BITRATE_MODE_AVERAGE:
                    description += " / ABR";
                    break;
                case SF_BITRATE_MODE_VARIABLE:
                    description += " / VBR";
                    break;
                default:
                    break;
            }
        }
        if (settings.bitrateKbps.has_value())
        {
            description += " / " + std::to_string(*settings.bitrateKbps) +
                           " kbps";
        }
        else if (settings.compressionLevel.has_value())
        {
            const int percent = static_cast<int>(
                std::lround(*settings.compressionLevel * 100.0));
            description += " / quality " + std::to_string(percent) + "%";
        }
    }

    return description;
}

cupuacu::SampleFormat
cupuacu::file::sampleFormatForSndfileFormat(const int sndfileFormat)
{
    const int subtype = sndfileFormat & SF_FORMAT_SUBMASK;

    switch (subtype)
    {
        case SF_FORMAT_PCM_S8:
            return cupuacu::SampleFormat::PCM_S8;
        case SF_FORMAT_PCM_16:
        case SF_FORMAT_ALAC_16:
            return cupuacu::SampleFormat::PCM_S16;
        case SF_FORMAT_PCM_24:
        case SF_FORMAT_ALAC_20:
        case SF_FORMAT_ALAC_24:
            return cupuacu::SampleFormat::PCM_S24;
        case SF_FORMAT_PCM_32:
        case SF_FORMAT_ALAC_32:
            return cupuacu::SampleFormat::PCM_S32;
        case SF_FORMAT_FLOAT:
            return cupuacu::SampleFormat::FLOAT32;
        case SF_FORMAT_DOUBLE:
            return cupuacu::SampleFormat::FLOAT64;
        case SF_FORMAT_ULAW:
        case SF_FORMAT_ALAW:
        case SF_FORMAT_IMA_ADPCM:
        case SF_FORMAT_MS_ADPCM:
        case SF_FORMAT_GSM610:
        case SF_FORMAT_VOX_ADPCM:
        case SF_FORMAT_NMS_ADPCM_16:
        case SF_FORMAT_NMS_ADPCM_24:
        case SF_FORMAT_NMS_ADPCM_32:
        case SF_FORMAT_G721_32:
        case SF_FORMAT_G723_24:
        case SF_FORMAT_G723_40:
        case SF_FORMAT_DWVW_12:
        case SF_FORMAT_DWVW_16:
        case SF_FORMAT_DWVW_24:
        case SF_FORMAT_DWVW_N:
        case SF_FORMAT_DPCM_8:
        case SF_FORMAT_DPCM_16:
        case SF_FORMAT_VORBIS:
        case SF_FORMAT_OPUS:
        case SF_FORMAT_MPEG_LAYER_I:
        case SF_FORMAT_MPEG_LAYER_II:
        case SF_FORMAT_MPEG_LAYER_III:
            return cupuacu::SampleFormat::FLOAT32;
        case SF_FORMAT_PCM_U8:
        default:
            return cupuacu::SampleFormat::Unknown;
    }
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

    return fallbackSettingsForPath(outputPath, documentFormat);
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

    for (const auto &candidate : kCandidateFormats)
    {
        if (candidate.majorFormat != major)
        {
            continue;
        }

        if (const auto fallback = makeSettings(candidate, subtype))
        {
            return fallback;
        }
    }

    return fallbackSettingsForPath(path, documentFormat);
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
