#include "Play.h"

#include "../CupuacuState.h"
#include "../gui/VuMeter.h"

#define MINIAUDIO_IMPLEMENTATION
#include "../miniaudio.h"

#include <cstring>
#include <thread>
#include <chrono>
#include <memory>
#include <atomic>
#include <mutex>

struct CustomDataSource {
    ma_data_source_base base;
    std::vector<const float*> channelData;
    ma_uint64 frameCount;
    ma_uint64 cursor;
    ma_uint64 start;
    ma_uint64 end;
    CupuacuState* state;
    ma_device device;
};

static ma_result custom_data_source_read(ma_data_source* pDataSource,
                                         void* pFramesOut,
                                         ma_uint64 frameCount,
                                         ma_uint64* pFramesRead)
{
    CustomDataSource* ds = (CustomDataSource*)pDataSource;
    ma_uint64 framesAvailable = ds->end - ds->cursor;
    ma_uint64 framesToRead = (frameCount < framesAvailable) ? frameCount : framesAvailable;

    if (framesToRead == 0)
    {
        *pFramesRead = 0;
        ds->state->isPlaying.store(false);
        return MA_AT_END;
    }

    float* out = (float*)pFramesOut;
    int64_t numChannels = ds->channelData.size();

    const bool selectionIsActive = ds->state->selection.isActive();
    const SelectedChannels selectedChannels = ds->state->selectedChannels;

    for (ma_uint64 i = 0; i < framesToRead; ++i)
    {
        float framePeak = 0.f;

        for (int64_t ch = 0; ch < numChannels; ++ch)
        {
            const bool shouldPlayChannel = !selectionIsActive ||
                selectedChannels == SelectedChannels::BOTH ||
                (ch == 0 && selectedChannels == SelectedChannels::LEFT) ||
                (ch == 1 && selectedChannels == SelectedChannels::RIGHT);

            float sample = shouldPlayChannel ? ds->channelData[ch][ds->cursor + i] : 0.f;
            out[i * numChannels + ch] = sample;
        }
    }

    for (int64_t ch = 0; ch < numChannels; ++ch)
    {
        ds->state->vuMeter->pushSamplesForChannelChunk(out, framesToRead, numChannels, ch);
    }

    ds->state->vuMeter->setSamplesPushed();

    ds->cursor += framesToRead;
    *pFramesRead = framesToRead;

    if (ds->state)
    {
        ds->state->playbackPosition.store((double)ds->cursor);
    }

    return MA_SUCCESS;
}

static ma_result custom_data_source_seek(ma_data_source *pDataSource, ma_uint64 frameIndex)
{
    CustomDataSource *ds = (CustomDataSource*)pDataSource;

    if (frameIndex < ds->start || frameIndex >= ds->end)
    {
        return MA_INVALID_ARGS;
    }

    ds->cursor = frameIndex;

    return MA_SUCCESS;
}

static ma_result custom_data_source_get_cursor(ma_data_source *pDataSource, ma_uint64 *pCursor)
{
    CustomDataSource *ds = (CustomDataSource*)pDataSource;
    *pCursor = ds->cursor;
    return MA_SUCCESS;
}

static ma_result custom_data_source_get_data_format(ma_data_source* pDataSource,
                                                    ma_format* pFormat,
                                                    ma_uint32* pChannels,
                                                    ma_uint32* pSampleRate,
                                                    ma_channel* pChannelMap,
                                                    size_t channelMapSize)
{
    CustomDataSource* ds = (CustomDataSource*)pDataSource;
    *pFormat = ma_format_f32;
    *pChannels = (ma_uint32)ds->channelData.size();
    *pSampleRate = (ma_uint32)ds->state->document.sampleRate;

    if (pChannelMap && channelMapSize >= *pChannels) {
        if (*pChannels == 1) {
            pChannelMap[0] = MA_CHANNEL_MONO;
        } else if (*pChannels == 2) {
            pChannelMap[0] = MA_CHANNEL_FRONT_LEFT;
            pChannelMap[1] = MA_CHANNEL_FRONT_RIGHT;
        }
    }

    return MA_SUCCESS;
}

static ma_data_source_vtable custom_data_source_vtable = {
    custom_data_source_read,
    custom_data_source_seek,
    custom_data_source_get_data_format,
    custom_data_source_get_cursor,
    NULL
};

static ma_result custom_data_source_init(CustomDataSource* ds,
                                         const std::vector<std::vector<float>>& channels,
                                         ma_uint64 start,
                                         ma_uint64 end,
                                         CupuacuState* state)
{
    ma_data_source_config config = ma_data_source_config_init();
    config.vtable = &custom_data_source_vtable;
    ma_result result = ma_data_source_init(&config, &ds->base);
    if (result != MA_SUCCESS) return result;

    ds->channelData.clear();
    for (const auto& ch : channels) {
        ds->channelData.push_back(ch.data());
    }

    ds->frameCount = channels.empty() ? 0 : channels[0].size();
    ds->cursor = start;
    ds->start = start;
    ds->end   = end;
    ds->state = state;

    return MA_SUCCESS;
}

static std::mutex playbackMutex;

static void ma_playback_callback(ma_device *pDevice, void *pOutput, const void *pInput, ma_uint32 frameCount)
{
    CustomDataSource *ds = (CustomDataSource*)pDevice->pUserData;
    ma_uint64 framesRead;
    ma_result result = ds->base.vtable->onRead((ma_data_source*)ds, pOutput, frameCount, &framesRead);

    if (result == MA_AT_END)
    {
        ds->state->isPlaying.store(false);
    }
}

void performStop(CupuacuState *state)
{
    std::shared_ptr<CustomDataSource> ds;

    {
        std::lock_guard<std::mutex> lock(playbackMutex);
        ds = state->activePlayback;
        state->activePlayback.reset();
        state->isPlaying.store(false);
    }

    if (!ds)
    {
        return;
    }

    if (ma_device_get_state(&ds->device) == ma_device_state_started)
    {
        ma_device_stop(&ds->device);
    }

    ma_device_uninit(&ds->device);
    ma_data_source_uninit(&ds->base);

    if (state->vuMeter)
    {
        state->vuMeter->startDecay();
    }
}

void play(CupuacuState *state)
{
    std::shared_ptr<CustomDataSource> ds;

    {
        std::lock_guard<std::mutex> lock(playbackMutex);
        auto prev = state->activePlayback;
        state->activePlayback.reset();
        state->isPlaying.store(false);

        if (prev) {
            std::thread([prev]() {
                if (ma_device_get_state(&prev->device) == ma_device_state_started) {
                    ma_device_stop(&prev->device);
                }
                ma_device_uninit(&prev->device);
                ma_data_source_uninit(&prev->base);
            }).detach();
        }
    }

    uint32_t channelCount = static_cast<uint32_t>(state->document.channels.size());
    if (channelCount == 0) return;
    if (channelCount > 2) channelCount = 2;

    const auto& sampleData = state->document.channels[0];
    ma_uint64 totalSamples = sampleData.size();
    ma_uint64 start = 0;
    ma_uint64 end = totalSamples;

    if (state->selection.isActive()) {
        start = (ma_uint64)state->selection.getStartInt();
        end   = (ma_uint64)state->selection.getEndInt();
    } else {
        start = (ma_uint64)state->cursor;
    }

    ds = std::make_shared<CustomDataSource>();

    state->playbackPosition.store(start);

    if (custom_data_source_init(ds.get(),
                                state->document.channels,
                                start,
                                end,
                                state) != MA_SUCCESS)
    {
        return;
    }

    ma_device_config deviceConfig = ma_device_config_init(ma_device_type_playback);
    deviceConfig.playback.format   = ma_format_f32;
    deviceConfig.playback.channels = channelCount;
    deviceConfig.sampleRate        = state->document.sampleRate;
    deviceConfig.dataCallback      = ma_playback_callback;
    deviceConfig.pUserData         = ds.get();

    if (ma_device_init(NULL, &deviceConfig, &ds->device) != MA_SUCCESS) {
        ma_data_source_uninit(&ds->base);
        return;
    }

    if (ma_device_start(&ds->device) != MA_SUCCESS) {
        ma_device_uninit(&ds->device);
        ma_data_source_uninit(&ds->base);
        return;
    }

    {
        std::lock_guard<std::mutex> lock(playbackMutex);
        state->activePlayback = ds;
        state->isPlaying.store(true);
    }

    std::thread([state, ds]() {
        while (state->isPlaying.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        performStop(state);
    }).detach();
}

void requestStop(CupuacuState *state)
{
    state->isPlaying.store(false);
}

