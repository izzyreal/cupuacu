#pragma once

#include "../CupuacuState.h"

#include <sndfile.hh>

#include <stdexcept>
#include <vector>

static void loadSampleData(CupuacuState *state)
{
    // Prepare SF_INFO
    SF_INFO sfinfo{};
    SNDFILE *snd = sf_open(state->currentFile.c_str(), SFM_READ, &sfinfo);
    if (!snd) {
        throw std::runtime_error("Failed to open file: " + state->currentFile);
    }

    // Fill metadata
    state->document.sampleRate = sfinfo.samplerate;

    int subtype = sfinfo.format & SF_FORMAT_SUBMASK;
    switch (subtype) {
        case SF_FORMAT_PCM_S8:   state->document.format = SampleFormat::PCM_S8; break;
        case SF_FORMAT_PCM_16:   state->document.format = SampleFormat::PCM_S16; break;
        case SF_FORMAT_PCM_24:   state->document.format = SampleFormat::PCM_S24; break;
        case SF_FORMAT_PCM_32:   state->document.format = SampleFormat::PCM_S32; break;
        case SF_FORMAT_FLOAT:    state->document.format = SampleFormat::FLOAT32; break;
        case SF_FORMAT_DOUBLE:   state->document.format = SampleFormat::FLOAT64; break;
        default:                 state->document.format = SampleFormat::Unknown; break;
    }

    int channels = sfinfo.channels;
    sf_count_t frames = sfinfo.frames;

    // Allocate per-channel storage
    state->document.channels.clear();
    state->document.channels.resize(channels, std::vector<float>(frames));

    // Read into interleaved float buffer
    std::vector<float> interleaved(frames * channels);
    sf_count_t framesRead = sf_readf_float(snd, interleaved.data(), frames);
    if (framesRead <= 0) {
        sf_close(snd);
        throw std::runtime_error("Failed to read samples from file: " + state->currentFile);
    }

    // Split into channels
    for (sf_count_t i = 0; i < framesRead; ++i) {
        for (int ch = 0; ch < channels; ++ch) {
            state->document.channels[ch][i] = interleaved[i * channels + ch];
        }
    }

    // Done with file
    sf_close(snd);

    state->selection.setHighest(state->document.getFrameCount());
}

