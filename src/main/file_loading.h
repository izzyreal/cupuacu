#pragma once

#include "CupuacuState.h"

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

static void loadSampleData(CupuacuState *state)
{
    ma_result result;
    ma_decoder decoder;

    result = ma_decoder_init_file(state->currentFile.c_str(), nullptr, &decoder);

    if (result != MA_SUCCESS)
    {
        throw std::runtime_error("Failed to load file: " + state->currentFile);
    }

    if (decoder.outputFormat != ma_format_s16)
    {
        ma_decoder_uninit(&decoder);
        throw std::runtime_error("Unsupported format: not s16 PCM");
    }

    if (decoder.outputChannels != 1 && decoder.outputChannels != 2)
    {
        ma_decoder_uninit(&decoder);
        throw std::runtime_error("Unsupported channel count");
    }

    ma_uint64 frameCount = 0;
    result = ma_decoder_get_length_in_pcm_frames(&decoder, &frameCount);

    if (result != MA_SUCCESS)
    {
        ma_decoder_uninit(&decoder);
        throw std::runtime_error("Failed to get frame count");
    }

    std::vector<int16_t> interleaved(frameCount * decoder.outputChannels);

    ma_uint64 framesRead = 0;
    result = ma_decoder_read_pcm_frames(&decoder, interleaved.data(), frameCount, &framesRead);

    if (result != MA_SUCCESS)
    {
        ma_decoder_uninit(&decoder);
        throw std::runtime_error("Failed to read PCM frames");
    }

    state->sampleDataL.clear();
    state->sampleDataR.clear();

    if (decoder.outputChannels == 1)
    {
        state->sampleDataL.assign(interleaved.begin(), interleaved.begin() + framesRead);
    }
    else
    {
        state->sampleDataL.reserve(framesRead);
        state->sampleDataR.reserve(framesRead);

        for (ma_uint64 i = 0; i < framesRead; ++i)
        {
            state->sampleDataL.push_back(interleaved[i * 2]);
            state->sampleDataR.push_back(interleaved[i * 2 + 1]);
        }
    }

    ma_decoder_uninit(&decoder);
}

