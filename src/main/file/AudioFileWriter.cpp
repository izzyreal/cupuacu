#include "AudioFileWriter.hpp"

#include "AudioExport.hpp"
#include "FileIo.hpp"
#include "SndfilePath.hpp"
#include "../storage/DocumentAudioReader.hpp"
#include "aiff/AiffMarkerMetadata.hpp"
#include "m4a/M4aAlacWriter.hpp"
#include "wav/WavMarkerMetadata.hpp"

#include <sndfile.h>

#include <algorithm>
#include <cerrno>
#include <filesystem>
#include <ios>
#include <stdexcept>
#include <vector>

namespace
{
    void
    applyEncodingSettings(SNDFILE *snd, const int sampleRate,
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
                const auto derivedLevel = [sampleRate,
                                           &settings]() -> std::optional<double>
                {
                    cupuacu::file::AudioExportSettings tmp = settings;
                    const auto options =
                        cupuacu::file::bitrateOptionsForSettings(tmp,
                                                                 sampleRate);
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
                                (static_cast<double>(option.value -
                                                     minBitrate) /
                                 static_cast<double>(maxBitrate - minBitrate));
                            return std::clamp(normalized, 0.0, 1.0);
                        }
                    }
                    return std::nullopt;
                }();
                if (derivedLevel.has_value())
                {
                    double compressionLevel = *derivedLevel;
                    sf_command(snd, SFC_SET_COMPRESSION_LEVEL,
                               &compressionLevel, sizeof(compressionLevel));
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
    const AudioExportSettings &settings, WriteProgressCallback progress)
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
    const auto reader = session.getAudioReader();
    const auto lease = session.document.acquireReadLease();
    writeFile(*reader, lease.getMarkers(), outputPath, settings,
              std::move(progress));
}

void cupuacu::file::AudioFileWriter::writeFile(
    const cupuacu::Document::ReadLease &document,
    const std::filesystem::path &outputPath,
    const AudioExportSettings &settings, WriteProgressCallback progress)
{
    storage::DocumentAudioReader reader(document);
    writeFile(reader, document.getMarkers(), outputPath, settings,
              std::move(progress));
}

void cupuacu::file::AudioFileWriter::writeFile(
    const storage::AudioReader &audio,
    const std::vector<DocumentMarker> &markers,
    const std::filesystem::path &outputPath,
    const AudioExportSettings &settings, WriteProgressCallback progress)
{
    if (outputPath.empty())
    {
        throw std::invalid_argument("Output path is empty");
    }
    if (!settings.isValid())
    {
        throw std::invalid_argument("Export settings are invalid");
    }

    if (cupuacu::file::isNativeM4aAlacExportSettings(settings))
    {
        cupuacu::file::m4a::writeAlacM4aFile(
            audio, markers, outputPath,
            cupuacu::file::m4aAlacBitDepthForSettings(settings), progress);
        return;
    }

    const int channels = audio.shape().channels;
    const int sampleRate = audio.shape().sampleRate;

    if (channels <= 0 || sampleRate <= 0 || audio.shape().frames < 0)
    {
        throw std::invalid_argument("Document format cannot be exported");
    }

    SF_INFO sfinfo{};
    sfinfo.channels = channels;
    sfinfo.samplerate = sampleRate;
    sfinfo.format = settings.sndfileFormat();
    if (sf_format_check(&sfinfo) == 0)
    {
        throw std::invalid_argument(
            "Export format is not supported by libsndfile");
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

            std::unique_ptr<SNDFILE, decltype(&sf_close)> fileHandle(snd,
                                                                     sf_close);
            applyEncodingSettings(snd, sampleRate, settings);

            const sf_count_t frames = audio.shape().frames;
            constexpr sf_count_t chunkFrames = 65536;
            std::vector<float> interleaved(
                static_cast<std::size_t>(chunkFrames) *
                static_cast<std::size_t>(channels));
            std::vector<float> channelSamples(chunkFrames);
            sf_count_t totalWritten = 0;
            if (progress)
            {
                progress(outputPath.string(), 0.0);
            }

            for (sf_count_t startFrame = 0; startFrame < frames;
                 startFrame += chunkFrames)
            {
                const sf_count_t framesToWrite =
                    std::min(chunkFrames, frames - startFrame);
                for (int channel = 0; channel < channels; ++channel)
                {
                    audio.readChannel(
                        channel, startFrame,
                        {channelSamples.data(),
                         static_cast<std::size_t>(framesToWrite)});
                    for (sf_count_t frame = 0; frame < framesToWrite; ++frame)
                    {
                        interleaved[static_cast<std::size_t>(frame) * channels +
                                    channel] = channelSamples[frame];
                    }
                }

                const sf_count_t written =
                    sf_writef_float(snd, interleaved.data(), framesToWrite);
                totalWritten += written;
                if (written != framesToWrite)
                {
                    break;
                }
                if (progress)
                {
                    progress(outputPath.string(),
                             frames > 0
                                 ? std::optional<double>(
                                       static_cast<double>(totalWritten) /
                                       static_cast<double>(frames))
                                 : std::optional<double>(1.0));
                }
            }
            const std::string writeDetail = sf_strerror(snd);
            sf_write_sync(snd);
            const int closeStatus = sf_close(fileHandle.release());
            if (closeStatus != 0)
            {
                throw detail::makeIoFailure("Failed to close output audio file",
                                            sf_error_number(closeStatus));
            }

            if (totalWritten != frames)
            {
                throw cupuacu::file::detail::makeIoFailure(
                    "Failed to write all audio frames", writeDetail);
            }

            if (!markers.empty() && settings.majorFormat == SF_FORMAT_WAV)
            {
                cupuacu::file::wav::markers::rewriteFileWithMarkers(
                    temporaryPath, markers);
            }
            else if (!markers.empty() && settings.majorFormat == SF_FORMAT_AIFF)
            {
                cupuacu::file::aiff::markers::rewriteFileWithMarkers(
                    temporaryPath, markers);
            }
        });
}
