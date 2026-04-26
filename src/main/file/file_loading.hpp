#pragma once

#include "../State.hpp"
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
#include <stdexcept>
#include <string>
#include <vector>

namespace cupuacu::file
{
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

        static void finalizeLoadedSession(cupuacu::State *state,
                                          const bool canMarkSavedSource)
        {
            auto &session = state->getActiveDocumentSession();
            if (canMarkSavedSource)
            {
                session.document.markCurrentStateAsSavedSource();
            }
            cupuacu::file::OverwritePreservation::refreshActiveSession(state);

            session.selection.reset();
            session.cursor = 0;
            session.syncSelectionAndCursorToDocumentLength();
        }

        static bool loadNativeAlacM4aIfApplicable(cupuacu::State *state)
        {
            auto &session = state->getActiveDocumentSession();
            if (!hasM4aExtension(session.currentFile))
            {
                return false;
            }

            const auto audio =
                cupuacu::file::m4a::readAlacM4aFile(session.currentFile);
            auto &doc = session.document;
            session.currentFileExportSettings =
                inferExportSettingsForFile(session.currentFile,
                                           CUPUACU_FORMAT_M4A |
                                               (audio.bitDepth == 24
                                                    ? CUPUACU_FORMAT_ALAC_24
                                                    : audio.bitDepth == 32
                                                          ? CUPUACU_FORMAT_ALAC_32
                                                          : CUPUACU_FORMAT_ALAC_16),
                                           audio.sampleFormat);
            session.setPreservationReference(session.currentFile,
                                             session.currentFileExportSettings);

            doc.initialize(audio.sampleFormat, audio.sampleRate,
                           audio.channels, audio.frameCount);

            const auto expectedSamples =
                static_cast<std::size_t>(audio.frameCount) * audio.channels;
            const auto expectedBytes =
                expectedSamples *
                static_cast<std::size_t>((audio.bitDepth + 7u) / 8u);
            if (audio.interleavedPcmBytes.size() != expectedBytes)
            {
                throw std::runtime_error("Decoded ALAC M4A sample count mismatch");
            }

            for (std::uint32_t frame = 0; frame < audio.frameCount; ++frame)
            {
                for (std::uint16_t ch = 0; ch < audio.channels; ++ch)
                {
                    const auto sampleIndex =
                        static_cast<std::size_t>(frame) * audio.channels +
                        static_cast<std::size_t>(ch);
                    doc.setSample(ch, frame,
                                  normalizedPcmToFloat(
                                      readNativePcmSample(
                                          audio.interleavedPcmBytes,
                                          sampleIndex, audio.bitDepth),
                                      audio.bitDepth),
                                  false);
                }
            }
            doc.replaceMarkers(audio.markers);
            finalizeLoadedSession(state, true);
            return true;
        }
    } // namespace detail

    static void loadSampleData(cupuacu::State *state)
    {
        auto &session = state->getActiveDocumentSession();
        auto &doc = session.document;

        if (detail::loadNativeAlacM4aIfApplicable(state))
        {
            return;
        }

        // Prepare SF_INFO
        SF_INFO sfinfo{};
        SNDFILE *snd =
            openSndfile(std::filesystem::path(session.currentFile), SFM_READ,
                        &sfinfo);
        if (!snd)
        {
            std::string detail = sf_strerror(nullptr);
            if (detail.empty() || detail == "No Error.")
            {
                detail = detail::describeErrno(errno);
            }
            throw std::runtime_error("Failed to open file: " +
                                     session.currentFile + ": " + detail);
        }

        const SampleFormat sampleFormat =
            sampleFormatForSndfileFormat(sfinfo.format);

        int channels = sfinfo.channels;
        sf_count_t frames = sfinfo.frames;

        session.currentFileExportSettings =
            inferExportSettingsForFile(session.currentFile, sfinfo.format,
                                       sampleFormat);
        session.setPreservationReference(session.currentFile,
                                         session.currentFileExportSettings);

        doc.initialize(sampleFormat, sfinfo.samplerate, channels, frames);

        // Read into interleaved float buffer
        std::vector<float> interleaved(frames * channels);
        sf_count_t framesRead = sf_readf_float(snd, interleaved.data(), frames);
        if (framesRead <= 0)
        {
            const std::string detail = sf_strerror(snd);
            sf_close(snd);
            throw std::runtime_error("Failed to read samples from file: " +
                                     session.currentFile + ": " + detail);
        }

        for (sf_count_t i = 0; i < framesRead; ++i)
        {
            for (int ch = 0; ch < channels; ++ch)
            {
                doc.setSample(ch, i, interleaved[i * channels + ch], false);
            }
        }

        // Done with file
        sf_close(snd);

        try
        {
            const int majorFormat = sfinfo.format & SF_FORMAT_TYPEMASK;
            if (majorFormat == SF_FORMAT_WAV)
            {
                doc.replaceMarkers(
                    cupuacu::file::wav::markers::readMarkers(session.currentFile));
            }
            else if (majorFormat == SF_FORMAT_AIFF)
            {
                doc.replaceMarkers(
                    cupuacu::file::aiff::markers::readMarkers(session.currentFile));
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
        cupuacu::file::OverwritePreservation::refreshActiveSession(state);

        session.selection.reset();
        session.cursor = 0;
        session.syncSelectionAndCursorToDocumentLength();
    }
} // namespace cupuacu::file
