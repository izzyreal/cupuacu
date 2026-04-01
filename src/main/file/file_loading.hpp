#pragma once

#include "../State.hpp"
#include "AudioExport.hpp"
#include "FileIo.hpp"
#include "SndfilePath.hpp"

#include <sndfile.hh>

#include <cerrno>
#include <stdexcept>
#include <vector>

namespace cupuacu::file
{
    static void loadSampleData(cupuacu::State *state)
    {
        auto &session = state->getActiveDocumentSession();
        auto &doc = session.document;

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

        session.selection.reset();
        session.cursor = 0;
        session.syncSelectionAndCursorToDocumentLength();
    }
} // namespace cupuacu::file
