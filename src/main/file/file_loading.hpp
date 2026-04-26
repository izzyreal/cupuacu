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

        static float pcm16ToFloat(const std::int16_t value)
        {
            return static_cast<float>(value) / 32768.0f;
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
                                               CUPUACU_FORMAT_ALAC,
                                           cupuacu::SampleFormat::PCM_S16);
            session.setPreservationReference(session.currentFile,
                                             session.currentFileExportSettings);

            doc.initialize(cupuacu::SampleFormat::PCM_S16, audio.sampleRate,
                           audio.channels, audio.frameCount);

            const auto expectedSamples =
                static_cast<std::size_t>(audio.frameCount) * audio.channels;
            if (audio.interleavedPcm16Samples.size() != expectedSamples)
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
                                  pcm16ToFloat(
                                      audio.interleavedPcm16Samples[sampleIndex]),
                                  false);
                }
            }
            doc.clearMarkers();
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
