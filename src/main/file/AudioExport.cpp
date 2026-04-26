#include "AudioExport.hpp"
#include "MarkerPersistence.hpp"

#include <sndfile.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
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
        const CandidateEncoding *encodings;
        std::size_t encodingCount;
    };

    constexpr CandidateEncoding kWavEncodings[] = {
        {"16-bit PCM", SF_FORMAT_PCM_16, "wav"},
        {"24-bit PCM", SF_FORMAT_PCM_24, "wav"},
        {"32-bit PCM", SF_FORMAT_PCM_32, "wav"},
        {"32-bit float", SF_FORMAT_FLOAT, "wav"},
    };
    constexpr CandidateEncoding kAiffEncodings[] = {
        {"16-bit PCM", SF_FORMAT_PCM_16, "aiff"},
        {"24-bit PCM", SF_FORMAT_PCM_24, "aiff"},
        {"32-bit PCM", SF_FORMAT_PCM_32, "aiff"},
        {"32-bit float", SF_FORMAT_FLOAT, "aiff"},
    };
    constexpr CandidateEncoding kCafEncodings[] = {
        {"16-bit ALAC", SF_FORMAT_ALAC_16, "caf"},
        {"24-bit ALAC", SF_FORMAT_ALAC_24, "caf"},
        {"32-bit ALAC", SF_FORMAT_ALAC_32, "caf"},
    };
    constexpr CandidateEncoding kM4aEncodings[] = {
        {"16-bit ALAC", cupuacu::file::CUPUACU_FORMAT_ALAC, "m4a"},
    };
    constexpr CandidateEncoding kFlacEncodings[] = {
        {"16-bit FLAC", SF_FORMAT_PCM_16, "flac"},
        {"24-bit FLAC", SF_FORMAT_PCM_24, "flac"},
    };
    constexpr CandidateEncoding kOggEncodings[] = {
        {"Default quality", SF_FORMAT_VORBIS, "ogg"},
    };
    constexpr CandidateEncoding kMpegEncodings[] = {
        {"Default quality", SF_FORMAT_MPEG_LAYER_III, "mp3"},
    };

    constexpr CandidateFormat kCandidateFormats[] = {
        {AudioExportContainer::WAV,
         AudioExportCodec::PCM,
         "WAV",
         "PCM",
         SF_FORMAT_WAV,
         kWavEncodings,
         std::size(kWavEncodings)},
        {AudioExportContainer::AIFF,
         AudioExportCodec::PCM,
         "AIFF",
         "PCM",
         SF_FORMAT_AIFF,
         kAiffEncodings,
         std::size(kAiffEncodings)},
        {AudioExportContainer::CAF,
         AudioExportCodec::ALAC,
         "CAF",
         "ALAC",
         SF_FORMAT_CAF,
         kCafEncodings,
         std::size(kCafEncodings)},
        {AudioExportContainer::M4A,
         AudioExportCodec::ALAC,
         "M4A",
         "ALAC",
         cupuacu::file::CUPUACU_FORMAT_M4A,
         kM4aEncodings,
         std::size(kM4aEncodings)},
        {AudioExportContainer::FLAC,
         AudioExportCodec::FLAC,
         "FLAC",
         "FLAC",
         SF_FORMAT_FLAC,
         kFlacEncodings,
         std::size(kFlacEncodings)},
        {AudioExportContainer::OGG,
         AudioExportCodec::VORBIS,
         "OGG",
         "Vorbis",
         SF_FORMAT_OGG,
         kOggEncodings,
         std::size(kOggEncodings)},
        {AudioExportContainer::MPEG,
         AudioExportCodec::MP3,
         "MPEG",
         "MP3",
         SF_FORMAT_MPEG,
         kMpegEncodings,
         std::size(kMpegEncodings)},
    };

    struct CandidateOpenFormat
    {
        int majorFormat;
        const char *label;
        const char *const *extensions;
        std::size_t extensionCount;
    };

    constexpr const char *kWavOpenExtensions[] = {"wav"};
    constexpr const char *kAiffOpenExtensions[] = {"aiff", "aif", "aifc"};
    constexpr const char *kCafOpenExtensions[] = {"caf"};
    constexpr const char *kM4aOpenExtensions[] = {"m4a", "mp4"};
    constexpr const char *kFlacOpenExtensions[] = {"flac"};
    constexpr const char *kOggOpenExtensions[] = {"ogg", "oga"};
    constexpr const char *kMpegOpenExtensions[] = {"mp3"};

    constexpr CandidateOpenFormat kCandidateOpenFormats[] = {
        {SF_FORMAT_WAV, "WAV audio", kWavOpenExtensions,
         std::size(kWavOpenExtensions)},
        {SF_FORMAT_AIFF, "AIFF audio", kAiffOpenExtensions,
         std::size(kAiffOpenExtensions)},
        {SF_FORMAT_CAF, "CAF audio", kCafOpenExtensions,
         std::size(kCafOpenExtensions)},
        {cupuacu::file::CUPUACU_FORMAT_M4A, "M4A ALAC audio",
         kM4aOpenExtensions, std::size(kM4aOpenExtensions)},
        {SF_FORMAT_FLAC, "FLAC audio", kFlacOpenExtensions,
         std::size(kFlacOpenExtensions)},
        {SF_FORMAT_OGG, "OGG audio", kOggOpenExtensions,
         std::size(kOggOpenExtensions)},
        {SF_FORMAT_MPEG, "MP3 audio", kMpegOpenExtensions,
         std::size(kMpegOpenExtensions)},
    };

    bool isFormatWritable(const int combinedFormat)
    {
        if (combinedFormat ==
            (cupuacu::file::CUPUACU_FORMAT_M4A |
             cupuacu::file::CUPUACU_FORMAT_ALAC))
        {
            return true;
        }

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
                if (option.container == AudioExportContainer::M4A)
                {
                    return cupuacu::file::CUPUACU_FORMAT_ALAC;
                }
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
        const int *bitrates = nullptr;
        std::size_t bitrateCount = 0;
    };

    constexpr int kMpeg1Bitrates[] = {32, 40, 48, 56, 64, 80, 96, 112,
                                      128, 160, 192, 224, 256, 320};
    constexpr int kMpeg2Bitrates[] = {8,  16, 24, 32, 40, 48, 56,
                                      64, 80, 96, 112, 128, 144, 160};
    constexpr int kMpeg25Bitrates[] = {8, 16, 24, 32, 40, 48, 56, 64};

    constexpr MpegBitrateBand kMpegBitrateBands[] = {
        {32000, std::numeric_limits<int>::max(), 32, 320, kMpeg1Bitrates,
         std::size(kMpeg1Bitrates)},
        {16000, 32000, 8, 160, kMpeg2Bitrates, std::size(kMpeg2Bitrates)},
        {0, 16000, 8, 64, kMpeg25Bitrates, std::size(kMpeg25Bitrates)},
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

        for (std::size_t i = 0; i < candidate.encodingCount; ++i)
        {
            const auto &encoding = candidate.encodings[i];
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

        if (candidate.encodingCount > 0)
        {
            return makeSettings(candidate, candidate.encodings[0].subtype);
        }

        return std::nullopt;
    }

    std::optional<AudioExportSettings>
    fallbackSettingsForPath(const std::filesystem::path &outputPath,
                            const cupuacu::SampleFormat documentFormat)
    {
        for (const auto &candidate : kCandidateFormats)
        {
            for (std::size_t i = 0; i < candidate.encodingCount; ++i)
            {
                const auto &encoding = candidate.encodings[i];
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

        for (std::size_t i = 0; i < candidate.encodingCount; ++i)
        {
            const auto &encoding = candidate.encodings[i];
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
        option.extensions.assign(candidate.extensions,
                                 candidate.extensions + candidate.extensionCount);
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
    result.reserve(band->bitrateCount);
    for (std::size_t i = 0; i < band->bitrateCount; ++i)
    {
        const int bitrate = band->bitrates[i];
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

std::string cupuacu::file::describeExportSettings(
    const AudioExportSettings &settings, const cupuacu::Document &document)
{
    std::string description = describeExportSettings(settings);
    const auto markerAssessment =
        cupuacu::file::assessMarkerPersistenceForSettings(document, settings);

    if (!description.empty())
    {
        description += "\n";
    }

    switch (markerAssessment.fidelity)
    {
        case cupuacu::file::MarkerPersistenceFidelity::Exact:
            description += "Markers: native support, exact round-trip.";
            break;
        case cupuacu::file::MarkerPersistenceFidelity::Lossy:
            description += "Markers: native support, but some marker data may be truncated.";
            break;
        case cupuacu::file::MarkerPersistenceFidelity::Unsupported:
        default:
            if (markerAssessment.requiresSidecarOrSessionFallback)
            {
                description +=
                    "Markers: no native support; Cupuacu fallback persistence is needed to keep markers.";
            }
            else
            {
                description += "Markers: no native support.";
            }
            break;
    }

    return description;
}

cupuacu::SampleFormat
cupuacu::file::sampleFormatForSndfileFormat(const int sndfileFormat)
{
    const int subtype = sndfileFormat & SF_FORMAT_SUBMASK;

    if (sndfileFormat ==
        (cupuacu::file::CUPUACU_FORMAT_M4A |
         cupuacu::file::CUPUACU_FORMAT_ALAC))
    {
        return cupuacu::SampleFormat::PCM_S16;
    }

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
            return cupuacu::SampleFormat::PCM_S8;
        default:
            return cupuacu::SampleFormat::Unknown;
    }
}

std::optional<AudioExportSettings>
cupuacu::file::defaultExportSettingsForPath(
    const std::filesystem::path &outputPath,
    const cupuacu::SampleFormat documentFormat)
{
    return fallbackSettingsForPath(outputPath, documentFormat);
}

std::optional<AudioExportSettings>
cupuacu::file::inferExportSettingsForFile(const std::filesystem::path &path,
                                          const int sndfileFormat,
                                          const cupuacu::SampleFormat documentFormat)
{
    const int major = sndfileFormat & SF_FORMAT_TYPEMASK;
    const int subtype = sndfileFormat & SF_FORMAT_SUBMASK;

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
           (settings.subtype == SF_FORMAT_PCM_U8 ||
            settings.subtype == SF_FORMAT_PCM_S8 ||
            settings.subtype == SF_FORMAT_PCM_16 ||
            settings.subtype == SF_FORMAT_FLOAT);
}

bool cupuacu::file::isNativeM4aAlacExportSettings(
    const AudioExportSettings &settings)
{
    return settings.container == AudioExportContainer::M4A &&
           settings.codec == AudioExportCodec::ALAC &&
           settings.majorFormat == CUPUACU_FORMAT_M4A &&
           settings.subtype == CUPUACU_FORMAT_ALAC;
}
