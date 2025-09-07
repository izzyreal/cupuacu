#pragma once

#include "../CupuacuState.h"
#include "../miniaudio.h"
#include <vector>
#include <cstring>
#include <thread>
#include <chrono>

typedef struct {
    ma_data_source_base base;
    const int16_t* sampleData;
    ma_uint64 sampleCount;
    ma_uint64 cursor;
} CustomDataSource;

static ma_result custom_data_source_read(ma_data_source* pDataSource,
                                         void* pFramesOut,
                                         ma_uint64 frameCount,
                                         ma_uint64* pFramesRead) {
    CustomDataSource* ds = (CustomDataSource*)pDataSource;
    ma_uint64 framesAvailable = ds->sampleCount - ds->cursor;
    ma_uint64 framesToRead = frameCount < framesAvailable ? frameCount : framesAvailable;

    if (framesToRead == 0) {
        *pFramesRead = 0;
        return MA_AT_END;
    }

    float* out = (float*)pFramesOut;
    const int16_t* in = ds->sampleData + ds->cursor;
    for (ma_uint64 i = 0; i < framesToRead; i++) {
        out[i] = in[i] / 32768.0f; // convert int16 -> float
    }

    ds->cursor += framesToRead;
    *pFramesRead = framesToRead;
    return MA_SUCCESS;
}

static ma_result custom_data_source_seek(ma_data_source* pDataSource, ma_uint64 frameIndex) {
    CustomDataSource* ds = (CustomDataSource*)pDataSource;
    if (frameIndex >= ds->sampleCount) return MA_INVALID_ARGS;
    ds->cursor = frameIndex;
    return MA_SUCCESS;
}

static ma_result custom_data_source_get_cursor(ma_data_source* pDataSource, ma_uint64* pCursor) {
    CustomDataSource* ds = (CustomDataSource*)pDataSource;
    *pCursor = ds->cursor;
    return MA_SUCCESS;
}

static ma_result custom_data_source_get_data_format(ma_data_source* pDataSource,
                                                    ma_format* pFormat,
                                                    ma_uint32* pChannels,
                                                    ma_uint32* pSampleRate,
                                                    ma_channel* pChannelMap,
                                                    size_t channelMapSize) {
    *pFormat = ma_format_f32; // output float
    *pChannels = 1;           // mono
    *pSampleRate = 44100;
    if (pChannelMap && channelMapSize >= 1) {
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

static ma_result custom_data_source_init(CustomDataSource* ds,
                                         const int16_t* sampleData,
                                         ma_uint64 sampleCount) {
    ma_data_source_config config = ma_data_source_config_init();
    config.vtable = &custom_data_source_vtable;
    ma_result result = ma_data_source_init(&config, &ds->base);
    if (result != MA_SUCCESS) return result;

    ds->sampleData = sampleData;
    ds->sampleCount = sampleCount;
    ds->cursor = 0;
    return MA_SUCCESS;
}

static void play(CupuacuState *state) {
    const auto& sampleData = state->sampleDataL;
    ma_uint64 sampleCount = sampleData.size();

    CustomDataSource ds;
    if (custom_data_source_init(&ds, sampleData.data(), sampleCount) != MA_SUCCESS) {
        return;
    }

    ma_device_config deviceConfig = ma_device_config_init(ma_device_type_playback);
    deviceConfig.playback.format = ma_format_f32;
    deviceConfig.playback.channels = 1;
    deviceConfig.sampleRate = 44100;

    deviceConfig.dataCallback = [](ma_device* pDevice, void* pOutput,
                                   const void* pInput, ma_uint32 frameCount) {
        CustomDataSource* ds = (CustomDataSource*)pDevice->pUserData;
        ma_uint64 framesRead;
        ds->base.vtable->onRead((ma_data_source*)ds, pOutput, frameCount, &framesRead);
    };
    deviceConfig.pUserData = &ds;

    ma_device device;
    if (ma_device_init(NULL, &deviceConfig, &device) != MA_SUCCESS) {
        ma_data_source_uninit(&ds.base);
        return;
    }

    if (ma_device_start(&device) != MA_SUCCESS) {
        ma_device_uninit(&device);
        ma_data_source_uninit(&ds.base);
        return;
    }

    while (ds.cursor < ds.sampleCount) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    ma_device_stop(&device);
    ma_device_uninit(&device);
    ma_data_source_uninit(&ds.base);
}

