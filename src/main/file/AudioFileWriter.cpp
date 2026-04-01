#include "AudioFileWriter.hpp"

#include "AudioExport.hpp"
#include "FileIo.hpp"
#include "SndfilePath.hpp"

#include <sndfile.h>

#include <algorithm>
#include <cerrno>
#include <filesystem>
#include <ios>
#include <stdexcept>
#include <vector>

namespace
{
    void applyEncodingSettings(SNDFILE *snd,
                               const int sampleRate,
                               const cupuacu::file::AudioExportSettings &settings)
    {
        if (snd == nullptr)
        {
            return;
        }

        if (settings.bitrateMode.has_value())
        {
            int bitrateMode = *settings.bitrateMode;
            sf_command(snd, SFC_SET_BITRATE_MODE, &bitrateMode,
                       sizeof(bitrateMode));
        }

        if (settings.bitrateKbps.has_value())
        {
            const auto bitrateOptions =
                cupuacu::file::bitrateOptionsForSettings(settings, sampleRate);
            if (!bitrateOptions.empty())
            {
                const auto derivedLevel =
                    [sampleRate, &settings]() -> std::optional<double>
                {
                    cupuacu::file::AudioExportSettings tmp = settings;
                    const auto options =
                        cupuacu::file::bitrateOptionsForSettings(tmp, sampleRate);
                    for (const auto &option : options)
                    {
                        if (option.value == *settings.bitrateKbps)
                        {
                            const int minBitrate = options.front().value;
                            const int maxBitrate = options.back().value;
                            if (maxBitrate <= minBitrate)
                            {
                                return std::nullopt;
                            }
                            const double normalized =
                                1.0 -
                                (static_cast<double>(option.value - minBitrate) /
                                 static_cast<double>(maxBitrate - minBitrate));
                            return std::clamp(normalized, 0.0, 1.0);
                        }
                    }
                    return std::nullopt;
                }();
                if (derivedLevel.has_value())
                {
                    double compressionLevel = *derivedLevel;
                    sf_command(snd, SFC_SET_COMPRESSION_LEVEL, &compressionLevel,
                               sizeof(compressionLevel));
                }
            }
        }
        else if (settings.compressionLevel.has_value())
        {
            double compressionLevel = *settings.compressionLevel;
            sf_command(snd, SFC_SET_COMPRESSION_LEVEL, &compressionLevel,
                       sizeof(compressionLevel));
        }
    }
} // namespace

void cupuacu::file::AudioFileWriter::writeFile(
    cupuacu::State *state, const std::filesystem::path &outputPath,
    const AudioExportSettings &settings)
{
    if (!state || outputPath.empty())
    {
        throw std::invalid_argument("Output path is empty");
    }
    if (!settings.isValid())
    {
        throw std::invalid_argument("Export settings are invalid");
    }

    const auto &session = state->getActiveDocumentSession();
    const auto &document = session.document;
    const int channels = document.getChannelCount();
    const int sampleRate = document.getSampleRate();

    if (channels <= 0 || sampleRate <= 0)
    {
        throw std::invalid_argument("Document format cannot be exported");
    }

    SF_INFO sfinfo{};
    sfinfo.channels = channels;
    sfinfo.samplerate = sampleRate;
    sfinfo.format = settings.sndfileFormat();
    if (sf_format_check(&sfinfo) == 0)
    {
        throw std::invalid_argument("Export format is not supported by libsndfile");
    }

    writeFileAtomically(
        outputPath,
        [&](const std::filesystem::path &temporaryPath)
        {
            SNDFILE *snd = openSndfile(temporaryPath, SFM_WRITE, &sfinfo);
            if (!snd)
            {
                std::string detail = sf_strerror(nullptr);
                if (detail.empty() || detail == "No Error.")
                {
                    detail = cupuacu::file::detail::describeErrno(errno);
                }
                throw cupuacu::file::detail::makeIoFailure(
                    "Failed to open output audio file", detail);
            }

            applyEncodingSettings(snd, sampleRate, settings);

            const sf_count_t frames = document.getFrameCount();
            std::vector<float> interleaved(static_cast<std::size_t>(frames) *
                                           static_cast<std::size_t>(channels));
            for (sf_count_t frame = 0; frame < frames; ++frame)
            {
                for (int channel = 0; channel < channels; ++channel)
                {
                    interleaved[static_cast<std::size_t>(frame) *
                                    static_cast<std::size_t>(channels) +
                                static_cast<std::size_t>(channel)] =
                        document.getSample(channel, frame);
                }
            }

            const sf_count_t written =
                sf_writef_float(snd, interleaved.data(), frames);
            const std::string writeDetail = sf_strerror(snd);
            sf_write_sync(snd);
            sf_close(snd);

            if (written != frames)
            {
                throw cupuacu::file::detail::makeIoFailure(
                    "Failed to write all audio frames", writeDetail);
            }
        });
}
