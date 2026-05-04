#pragma once

#include "../State.hpp"
#include "../LongTask.hpp"
#include "AudioExport.hpp"
#include "FileIo.hpp"
#include "OverwritePreservation.hpp"
#include "SndfilePath.hpp"
#include "aiff/AiffMarkerMetadata.hpp"
#include "m4a/M4aAlacReader.hpp"
#include "wav/WavMarkerMetadata.hpp"

#include <sndfile.hh>

#include <cerrno>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace cupuacu::file
{
    struct LoadedAudioFile
    {
        Document document;
        std::optional<AudioExportSettings> exportSettings;
    };

    using LoadProgressCallback =
        std::function<void(const std::string &, std::optional<double>)>;

    namespace detail
    {
        static bool hasM4aExtension(const std::filesystem::path &path)
        {
            auto extension = path.extension().string();
            for (auto &ch : extension)
            {
                ch = static_cast<char>(
                    std::tolower(static_cast<unsigned char>(ch)));
            }
            return extension == ".m4a" || extension == ".mp4";
        }

        static bool nativeLittleEndian()
        {
            const std::uint16_t value = 1;
            return *reinterpret_cast<const std::uint8_t *>(&value) == 1;
        }

        static std::int32_t readNativePcmSample(
            const std::vector<std::uint8_t> &bytes,
            const std::size_t sampleIndex,
            const std::uint16_t bitDepth)
        {
            const auto bytesPerSample = static_cast<std::size_t>(
                (bitDepth + 7u) / 8u);
            const auto offset = sampleIndex * bytesPerSample;
            switch (bitDepth)
            {
                case 16:
                {
                    std::int16_t value = 0;
                    std::memcpy(&value, bytes.data() + offset, sizeof(value));
                    return value;
                }
                case 24:
                {
                    std::uint32_t value = 0;
                    if (nativeLittleEndian())
                    {
                        value = static_cast<std::uint32_t>(bytes[offset]) |
                                (static_cast<std::uint32_t>(bytes[offset + 1])
                                 << 8u) |
                                (static_cast<std::uint32_t>(bytes[offset + 2])
                                 << 16u);
                    }
                    else
                    {
                        value = static_cast<std::uint32_t>(bytes[offset + 2]) |
                                (static_cast<std::uint32_t>(bytes[offset + 1])
                                 << 8u) |
                                (static_cast<std::uint32_t>(bytes[offset])
                                 << 16u);
                    }
                    if ((value & 0x00800000u) != 0u)
                    {
                        value |= 0xff000000u;
                    }
                    return static_cast<std::int32_t>(value);
                }
                case 32:
                {
                    std::int32_t value = 0;
                    std::memcpy(&value, bytes.data() + offset, sizeof(value));
                    return value;
                }
                default:
                    throw std::runtime_error(
                        "Unsupported ALAC M4A bit depth");
            }
        }

        static void convertNativePcm16BlockToFloat(
            const std::uint8_t *pcmBytes,
            const std::size_t sampleCount,
            float *output)
        {
            for (std::size_t i = 0; i < sampleCount; ++i)
            {
                std::int16_t value = 0;
                std::memcpy(&value, pcmBytes + i * sizeof(std::int16_t),
                            sizeof(value));
                output[i] = static_cast<float>(value) / 32768.0f;
            }
        }

        static void convertNativePcm24BlockToFloat(
            const std::uint8_t *pcmBytes,
            const std::size_t sampleCount,
            float *output)
        {
            for (std::size_t i = 0; i < sampleCount; ++i)
            {
                const auto offset = i * 3u;
                std::uint32_t value = 0;
                if (nativeLittleEndian())
                {
                    value = static_cast<std::uint32_t>(pcmBytes[offset]) |
                            (static_cast<std::uint32_t>(pcmBytes[offset + 1])
                             << 8u) |
                            (static_cast<std::uint32_t>(pcmBytes[offset + 2])
                             << 16u);
                }
                else
                {
                    value = static_cast<std::uint32_t>(pcmBytes[offset + 2]) |
                            (static_cast<std::uint32_t>(pcmBytes[offset + 1])
                             << 8u) |
                            (static_cast<std::uint32_t>(pcmBytes[offset])
                             << 16u);
                }
                if ((value & 0x00800000u) != 0u)
                {
                    value |= 0xff000000u;
                }
                output[i] = static_cast<float>(
                    static_cast<double>(static_cast<std::int32_t>(value)) /
                    8388608.0);
            }
        }

        static void convertNativePcm32BlockToFloat(
            const std::uint8_t *pcmBytes,
            const std::size_t sampleCount,
            float *output)
        {
            for (std::size_t i = 0; i < sampleCount; ++i)
            {
                std::int32_t value = 0;
                std::memcpy(&value, pcmBytes + i * sizeof(std::int32_t),
                            sizeof(value));
                output[i] = static_cast<float>(
                    static_cast<double>(value) / 2147483648.0);
            }
        }

        static float normalizedPcmToFloat(const std::int32_t value,
                                          const std::uint16_t bitDepth)
        {
            switch (bitDepth)
            {
                case 16:
                    return static_cast<float>(value) / 32768.0f;
                case 24:
                    return static_cast<float>(
                               static_cast<double>(value) / 8388608.0);
                case 32:
                    return static_cast<float>(
                               static_cast<double>(value) / 2147483648.0);
                default:
                    throw std::runtime_error(
                        "Unsupported ALAC M4A bit depth");
            }
        }

        static std::string formatLoadProgressDetail(
            const std::string &path, const sf_count_t framesRead,
            const sf_count_t totalFrames)
        {
            if (totalFrames <= 0)
            {
                return path;
            }

            const int percent = static_cast<int>(
                std::clamp<double>(
                    static_cast<double>(framesRead) /
                        static_cast<double>(totalFrames),
                    0.0, 1.0) *
                100.0);
            return path + " (" + std::to_string(percent) + "%)";
        }

        static void updateLoadProgress(const LoadProgressCallback &progress,
                                       const std::string &path,
                                       const sf_count_t framesRead,
                                       const sf_count_t totalFrames)
        {
            if (!progress)
            {
                return;
            }
            const std::optional<double> progressValue =
                totalFrames > 0
                    ? std::optional<double>{
                          std::clamp<double>(
                              static_cast<double>(framesRead) /
                                  static_cast<double>(totalFrames),
                              0.0, 1.0)}
                    : std::nullopt;
            progress(formatLoadProgressDetail(path, framesRead, totalFrames),
                     progressValue);
        }

        static std::optional<LoadedAudioFile> loadNativeAlacM4aIfApplicable(
            const std::string &path, const LoadProgressCallback &progress)
        {
            if (!hasM4aExtension(path))
            {
                return std::nullopt;
            }

            if (progress)
            {
                progress(path + " (reading file)", 0.0);
            }
            int lastDecodePercent = -1;
            int lastReadPercent = -1;
            bool decodeStarted = false;
            LoadedAudioFile result;
            auto &doc = result.document;
            std::uint32_t totalFramesLoaded = 0;
            constexpr std::uint32_t kLoadBlockFrames = 65536u;
            std::vector<float> interleaved;
            std::uint16_t streamChannels = 0;
            auto flushInterleaved = [&doc, &interleaved, &totalFramesLoaded,
                                     &streamChannels]()
            {
                if (interleaved.empty() || streamChannels == 0)
                {
                    return;
                }

                const auto frameCount = static_cast<std::uint32_t>(
                    interleaved.size() /
                    static_cast<std::size_t>(streamChannels));
                doc.writeInterleavedFloatBlock(totalFramesLoaded,
                                               interleaved.data(), frameCount,
                                               streamChannels, false);
                totalFramesLoaded += frameCount;
                interleaved.clear();
            };
            cupuacu::file::m4a::M4aAlacFileInfo fileInfo;
            fileInfo = cupuacu::file::m4a::streamAlacM4aFile(
                path,
                [&doc, &interleaved, &totalFramesLoaded, &streamChannels,
                 &flushInterleaved](
                    const std::uint8_t *interleavedPcmBytes,
                    const std::uint32_t pcmByteCount,
                    const std::uint32_t frameCount,
                    const std::uint16_t channels,
                    const std::uint16_t bitDepth)
                {
                    const auto sampleCount = static_cast<std::size_t>(frameCount) *
                                             static_cast<std::size_t>(channels);
                    if (streamChannels == 0)
                    {
                        streamChannels = channels;
                        interleaved.reserve(
                            static_cast<std::size_t>(kLoadBlockFrames) *
                            static_cast<std::size_t>(channels));
                    }
                    if (streamChannels != channels)
                    {
                        throw std::runtime_error(
                            "ALAC M4A channel count changed during decode");
                    }

                    const auto existingSamples = interleaved.size();
                    const auto incomingSamples = sampleCount;
                    const auto incomingFrames = frameCount;
                    const auto bufferedFrames = static_cast<std::uint32_t>(
                        existingSamples / static_cast<std::size_t>(channels));
                    if (bufferedFrames > 0 &&
                        bufferedFrames + incomingFrames > kLoadBlockFrames)
                    {
                        flushInterleaved();
                    }

                    const auto writeOffset = interleaved.size();
                    interleaved.resize(writeOffset + incomingSamples);
                    auto *const output = interleaved.data() + writeOffset;
                    switch (bitDepth)
                    {
                        case 16:
                            convertNativePcm16BlockToFloat(
                                interleavedPcmBytes, sampleCount, output);
                            break;
                        case 24:
                            convertNativePcm24BlockToFloat(
                                interleavedPcmBytes, sampleCount, output);
                            break;
                        case 32:
                            convertNativePcm32BlockToFloat(
                                interleavedPcmBytes, sampleCount, output);
                            break;
                        default:
                            throw std::runtime_error(
                                "Unsupported ALAC M4A bit depth");
                    }
                    const auto framesBuffered = static_cast<std::uint32_t>(
                        interleaved.size() /
                        static_cast<std::size_t>(channels));
                    if (framesBuffered >= kLoadBlockFrames)
                    {
                        flushInterleaved();
                    }
                },
                [&doc, &result, &path, &streamChannels, &interleaved](
                    const cupuacu::file::m4a::M4aAlacFileInfo &streamInfo)
                {
                    result.exportSettings = inferExportSettingsForFile(
                        path,
                        CUPUACU_FORMAT_M4A |
                            (streamInfo.bitDepth == 24
                                 ? CUPUACU_FORMAT_ALAC_24
                                 : streamInfo.bitDepth == 32
                                       ? CUPUACU_FORMAT_ALAC_32
                                       : CUPUACU_FORMAT_ALAC_16),
                        streamInfo.sampleFormat);
                    doc.initialize(streamInfo.sampleFormat,
                                   streamInfo.sampleRate, streamInfo.channels,
                                   streamInfo.frameCount);
                    streamChannels = streamInfo.channels;
                    interleaved.reserve(
                        static_cast<std::size_t>(kLoadBlockFrames) *
                        static_cast<std::size_t>(streamInfo.channels));
                },
                [progress, path, &lastDecodePercent, &decodeStarted](
                    const std::uint32_t decodedFrames,
                    const std::uint32_t totalFrames)
                {
                    if (!progress || totalFrames == 0)
                    {
                        return;
                    }
                    decodeStarted = true;
                    const int percent = static_cast<int>(
                        std::clamp<double>(
                            static_cast<double>(decodedFrames) /
                                static_cast<double>(totalFrames),
                            0.0, 1.0) *
                        100.0);
                    if (percent == lastDecodePercent && decodedFrames < totalFrames)
                    {
                        return;
                    }
                    lastDecodePercent = percent;
                    progress(
                        path + " (decoding ALAC " + std::to_string(percent) +
                            "%)",
                        0.05 +
                            std::clamp<double>(
                                static_cast<double>(decodedFrames) /
                                    static_cast<double>(totalFrames),
                                0.0, 1.0) *
                                0.95);
                },
                [progress, path, &lastReadPercent, &decodeStarted](
                    const std::uint64_t bytesRead,
                    const std::uint64_t totalBytes)
                {
                    if (!progress || totalBytes == 0 || decodeStarted)
                    {
                        return;
                    }
                    const int percent = static_cast<int>(
                        std::clamp<double>(
                            static_cast<double>(bytesRead) /
                                static_cast<double>(totalBytes),
                            0.0, 1.0) *
                        100.0);
                    if (percent == lastReadPercent && bytesRead < totalBytes)
                    {
                        return;
                    }
                    lastReadPercent = percent;
                    progress(
                        path + " (reading file " + std::to_string(percent) +
                            "%)",
                        std::clamp<double>(
                            static_cast<double>(bytesRead) /
                                static_cast<double>(totalBytes),
                            0.0, 1.0) *
                            0.05);
                });
            flushInterleaved();
            if (totalFramesLoaded != fileInfo.frameCount)
            {
                throw std::runtime_error("Decoded ALAC M4A sample count mismatch");
            }
            doc.replaceMarkers(std::move(fileInfo.markers));
            doc.markCurrentStateAsSavedSource();
            return result;
        }
    } // namespace detail

    static LoadedAudioFile loadAudioFile(
        const std::string &path, const LoadProgressCallback &progress = {})
    {
        if (auto loaded = detail::loadNativeAlacM4aIfApplicable(path, progress))
        {
            return std::move(*loaded);
        }

        // Prepare SF_INFO
        SF_INFO sfinfo{};
        SNDFILE *snd =
            openSndfile(std::filesystem::path(path), SFM_READ, &sfinfo);
        if (!snd)
        {
            std::string detail = sf_strerror(nullptr);
            if (detail.empty() || detail == "No Error.")
            {
                detail = detail::describeErrno(errno);
            }
            throw std::runtime_error("Failed to open file: " + path + ": " +
                                     detail);
        }

        const SampleFormat sampleFormat =
            sampleFormatForSndfileFormat(sfinfo.format);

        int channels = sfinfo.channels;
        sf_count_t frames = sfinfo.frames;

        LoadedAudioFile result;
        auto &doc = result.document;
        result.exportSettings =
            inferExportSettingsForFile(path, sfinfo.format, sampleFormat);

        doc.initialize(sampleFormat, sfinfo.samplerate, channels, frames);

        constexpr sf_count_t kLoadBlockFrames = 65536;
        std::vector<float> interleaved(
            static_cast<std::size_t>(kLoadBlockFrames) *
            static_cast<std::size_t>(channels));
        sf_count_t totalFramesRead = 0;
        detail::updateLoadProgress(progress, path, 0, frames);
        while (totalFramesRead < frames)
        {
            const sf_count_t framesToRead =
                std::min<sf_count_t>(kLoadBlockFrames, frames - totalFramesRead);
            const sf_count_t framesRead =
                sf_readf_float(snd, interleaved.data(), framesToRead);
            if (framesRead <= 0)
            {
                const std::string detail = sf_strerror(snd);
                sf_close(snd);
                throw std::runtime_error("Failed to read samples from file: " +
                                         path + ": " + detail);
            }

            doc.writeInterleavedFloatBlock(totalFramesRead, interleaved.data(),
                                           framesRead, channels, false);

            totalFramesRead += framesRead;
            detail::updateLoadProgress(progress, path, totalFramesRead, frames);
        }

        // Done with file
        sf_close(snd);

        try
        {
            const int majorFormat = sfinfo.format & SF_FORMAT_TYPEMASK;
            if (majorFormat == SF_FORMAT_WAV)
            {
                doc.replaceMarkers(
                    cupuacu::file::wav::markers::readMarkers(path));
            }
            else if (majorFormat == SF_FORMAT_AIFF)
            {
                doc.replaceMarkers(
                    cupuacu::file::aiff::markers::readMarkers(path));
            }
        }
        catch (...)
        {
            doc.clearMarkers();
        }

        if (sampleFormat == SampleFormat::PCM_S8 ||
            sampleFormat == SampleFormat::PCM_S16 ||
            sampleFormat == SampleFormat::FLOAT32)
        {
            doc.markCurrentStateAsSavedSource();
        }
        return result;
    }

    static void commitLoadedAudioFile(cupuacu::DocumentSession &session,
                                      const std::string &path,
                                      LoadedAudioFile loaded)
    {
        session.stopWaveformCacheBuild();
        session.currentFileExportSettings = loaded.exportSettings;
        session.setPreservationReference(path, session.currentFileExportSettings);
        session.document = std::move(loaded.document);
        session.waveformCaches.resetToChannelCount(
            session.document.getChannelCount());
        session.selection.reset();
        session.cursor = 0;
        session.syncSelectionAndCursorToDocumentLength();
    }

    static void loadSampleData(cupuacu::State *state)
    {
        auto &session = state->getActiveDocumentSession();
        const auto path = session.currentFile;
        auto loaded = loadAudioFile(
            path,
            [state](const std::string &detail,
                    const std::optional<double> progress)
            {
                cupuacu::updateLongTask(state, detail, progress, true);
            });
        commitLoadedAudioFile(session, path, std::move(loaded));
        cupuacu::file::OverwritePreservation::refreshActiveSession(state);
    }
} // namespace cupuacu::file
