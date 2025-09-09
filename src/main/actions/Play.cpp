#include "Play.h"

#include "../CupuacuState.h"

#include "../miniaudio.h"

#include <vector>
#include <cstring>
#include <thread>
#include <chrono>
#include <memory>
#include <atomic>
#include <mutex>

struct CustomDataSource {
    ma_data_source_base base;
    const int16_t* sampleData;
    ma_uint64 sampleCount;
    ma_uint64 cursor;
    ma_uint64 start;
    ma_uint64 end;
    CupuacuState* state;
    ma_device device;
};

static ma_result custom_data_source_read(ma_data_source *pDataSource,
                                         void *pFramesOut,
                                         ma_uint64 frameCount,
                                         ma_uint64 *pFramesRead)
{
    CustomDataSource *ds = (CustomDataSource*)pDataSource;
    ma_uint64 framesAvailable = ds->end - ds->cursor;
    ma_uint64 framesToRead = frameCount < framesAvailable ? frameCount : framesAvailable;

    if (framesToRead == 0)
    {
        *pFramesRead = 0;
    
        if (ds->state)
        {
            if (ds->state->selection.isActive())
            {
                ds->state->playbackPosition.store((double)ds->end);
            }
            else
            {
                ds->state->playbackPosition.store(0);
            }
            
            ds->state->isPlaying.store(false);
        }
        
        return MA_AT_END;
    }

    float *out = (float*)pFramesOut;
    const int16_t *in = ds->sampleData + ds->cursor;
    
    for (ma_uint64 i = 0; i < framesToRead; i++)
    {
        out[i] = in[i] / 32768.0f;
    }

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

static ma_result custom_data_source_get_data_format(ma_data_source *pDataSource,
                                                    ma_format *pFormat,
                                                    ma_uint32 *pChannels,
                                                    ma_uint32 *pSampleRate,
                                                    ma_channel *pChannelMap,
                                                    size_t channelMapSize)
{
    *pFormat = ma_format_f32;
    *pChannels = 1;
    *pSampleRate = 44100;

    if (pChannelMap && channelMapSize >= 1)
    {
        pChannelMap[0] = MA_CHANNEL_MONO;
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

static ma_result custom_data_source_init(CustomDataSource *ds,
                                         const int16_t *sampleData,
                                         ma_uint64 sampleCount,
                                         ma_uint64 start,
                                         ma_uint64 end,
                                         CupuacuState *state)
{
    ma_data_source_config config = ma_data_source_config_init();
    config.vtable = &custom_data_source_vtable;
    ma_result result = ma_data_source_init(&config, &ds->base);

    if (result != MA_SUCCESS)
    {
        return result;
    }

    ds->sampleData = sampleData;
    ds->sampleCount = sampleCount;
    ds->cursor = start;
    ds->start = start;
    ds->end = end;
    ds->state = state;

    return MA_SUCCESS;
}

static std::mutex playbackMutex;

static void ma_playback_callback(ma_device *pDevice, void *pOutput, const void *pInput, ma_uint32 frameCount)
{
    CustomDataSource *ds = (CustomDataSource*)pDevice->pUserData;
    ma_uint64 framesRead;
    ds->base.vtable->onRead((ma_data_source*)ds, pOutput, frameCount, &framesRead);
}

void stop(CupuacuState *state)
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
}

void play(CupuacuState *state)
{
    std::shared_ptr<CustomDataSource> ds;

    // Stop any previous playback safely without holding the lock while stopping the device
    {
        std::lock_guard<std::mutex> lock(playbackMutex);
        auto prev = state->activePlayback;
        state->activePlayback.reset();
        state->isPlaying.store(false);

        if (prev)
        {
            std::thread([prev]() {
                
                if (ma_device_get_state(&prev->device) == ma_device_state_started)
                {
                    ma_device_stop(&prev->device);
                }

                ma_device_uninit(&prev->device);
                ma_data_source_uninit(&prev->base);

            }).detach();
        }
    }

    const auto& sampleData = state->sampleDataL;
    ma_uint64 totalSamples = sampleData.size();
    ma_uint64 start = 0;
    ma_uint64 end = totalSamples;

    if (state->selection.isActive())
    {
        start = (ma_uint64)state->selection.getStartFloorInt();
        end = (ma_uint64)state->selection.getEndFloorInt();
    }
    else
    {
        start = (ma_uint64)state->playbackPosition.load();
    }

    ds = std::make_shared<CustomDataSource>();
    
    if (custom_data_source_init(ds.get(), sampleData.data(), totalSamples, start, end, state) != MA_SUCCESS)
    {
        return;
    }

    ma_device_config deviceConfig = ma_device_config_init(ma_device_type_playback);
    deviceConfig.playback.format = ma_format_f32;
    deviceConfig.playback.channels = 1;
    deviceConfig.sampleRate = 44100;
    deviceConfig.dataCallback = ma_playback_callback;
    deviceConfig.pUserData = ds.get();

    if (ma_device_init(NULL, &deviceConfig, &ds->device) != MA_SUCCESS)
    {
        ma_data_source_uninit(&ds->base);
        return;
    }

    if (ma_device_start(&ds->device) != MA_SUCCESS)
    {
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
        
        while (state->isPlaying.load())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        stop(state);
    }).detach();
}

