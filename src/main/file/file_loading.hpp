#pragma once

#include "../State.hpp"
#include "AudioExport.hpp"
#include "SndfilePath.hpp"

#include <sndfile.hh>

#include <cstdio>
#include <stdexcept>
#include <vector>

namespace cupuacu::file
{
    static void loadSampleData(cupuacu::State *state)
    {
        auto &session = state->getActiveDocumentSession();
        auto &doc = session.document;
        std::fprintf(stderr,
                     "CUPUACU_DEBUG_LOAD: begin path=%s\n",
                     session.currentFile.c_str());
        std::fflush(stderr);

        // Prepare SF_INFO
        SF_INFO sfinfo{};
        SNDFILE *snd =
            openSndfile(std::filesystem::path(session.currentFile), SFM_READ,
                        &sfinfo);
        std::fprintf(stderr,
                     "CUPUACU_DEBUG_LOAD: after open snd=%p samplerate=%d channels=%d frames=%lld format=0x%x\n",
                     static_cast<void *>(snd), sfinfo.samplerate, sfinfo.channels,
                     static_cast<long long>(sfinfo.frames), sfinfo.format);
        std::fflush(stderr);
        if (!snd)
        {
            throw std::runtime_error("Failed to open file: " +
                                     session.currentFile);
        }

        const SampleFormat sampleFormat =
            sampleFormatForSndfileFormat(sfinfo.format);
        std::fprintf(stderr,
                     "CUPUACU_DEBUG_LOAD: sampleFormat=%d\n",
                     static_cast<int>(sampleFormat));
        std::fflush(stderr);

        int channels = sfinfo.channels;
        sf_count_t frames = sfinfo.frames;

        session.currentFileExportSettings =
            inferExportSettingsForFile(session.currentFile, sfinfo.format,
                                       sampleFormat);
        std::fprintf(stderr,
                     "CUPUACU_DEBUG_LOAD: inferred settings valid=%d major=0x%x subtype=0x%x ext=%s\n",
                     session.currentFileExportSettings.has_value() ? 1 : 0,
                     session.currentFileExportSettings.has_value()
                         ? session.currentFileExportSettings->majorFormat
                         : 0,
                     session.currentFileExportSettings.has_value()
                         ? session.currentFileExportSettings->subtype
                         : 0,
                     session.currentFileExportSettings.has_value()
                         ? session.currentFileExportSettings->extension.c_str()
                         : "<none>");
        std::fflush(stderr);

        doc.initialize(sampleFormat, sfinfo.samplerate, channels, frames);
        std::fprintf(stderr,
                     "CUPUACU_DEBUG_LOAD: document initialized channels=%lld frames=%lld\n",
                     static_cast<long long>(doc.getChannelCount()),
                     static_cast<long long>(doc.getFrameCount()));
        std::fflush(stderr);

        // Read into interleaved float buffer
        std::vector<float> interleaved(frames * channels);
        sf_count_t framesRead = sf_readf_float(snd, interleaved.data(), frames);
        std::fprintf(stderr,
                     "CUPUACU_DEBUG_LOAD: framesRead=%lld\n",
                     static_cast<long long>(framesRead));
        std::fflush(stderr);
        if (framesRead <= 0)
        {
            sf_close(snd);
            throw std::runtime_error("Failed to read samples from file: " +
                                     session.currentFile);
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
        std::fprintf(stderr, "CUPUACU_DEBUG_LOAD: after close\n");
        std::fflush(stderr);

        session.selection.reset();
        session.cursor = 0;
        session.syncSelectionAndCursorToDocumentLength();
        std::fprintf(stderr, "CUPUACU_DEBUG_LOAD: finished\n");
        std::fflush(stderr);
    }
} // namespace cupuacu::file
