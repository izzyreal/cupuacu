#pragma once

#include "../State.h"

#include <sndfile.hh>

#include <stdexcept>
#include <vector>

namespace cupuacu::file {
static void loadSampleData(cupuacu::State *state)
{
    // Prepare SF_INFO
    SF_INFO sfinfo{};
    SNDFILE *snd = sf_open(state->currentFile.c_str(), SFM_READ, &sfinfo);
    if (!snd) {
        throw std::runtime_error("Failed to open file: " + state->currentFile);
    }

    int subtype = sfinfo.format & SF_FORMAT_SUBMASK;
    SampleFormat sampleFormat;

    switch (subtype) {
        case SF_FORMAT_PCM_S8:   sampleFormat = SampleFormat::PCM_S8; break;
        case SF_FORMAT_PCM_16:   sampleFormat = SampleFormat::PCM_S16; break;
        case SF_FORMAT_PCM_24:   sampleFormat = SampleFormat::PCM_S24; break;
        case SF_FORMAT_PCM_32:   sampleFormat = SampleFormat::PCM_S32; break;
        case SF_FORMAT_FLOAT:    sampleFormat = SampleFormat::FLOAT32; break;
        case SF_FORMAT_DOUBLE:   sampleFormat = SampleFormat::FLOAT64; break;
        default:                 sampleFormat = SampleFormat::Unknown; break;
    }

    int channels = sfinfo.channels;
    sf_count_t frames = sfinfo.frames;

    state->document.initialize(sampleFormat, sfinfo.samplerate, channels, frames);

    // Read into interleaved float buffer
    std::vector<float> interleaved(frames * channels);
    sf_count_t framesRead = sf_readf_float(snd, interleaved.data(), frames);
    if (framesRead <= 0) {
        sf_close(snd);
        throw std::runtime_error("Failed to read samples from file: " + state->currentFile);
    }

    for (sf_count_t i = 0; i < framesRead; ++i)
    {
        for (int ch = 0; ch < channels; ++ch)
        {
            state->document.setSample(ch, i, interleaved[i * channels + ch], false);
        }
    }

    // Done with file
    sf_close(snd);

    state->selection.setHighest(state->document.getFrameCount());
}
}

