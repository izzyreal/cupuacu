#include "audio/AudioDevices.hpp"
#include "audio/AudioCallbackCore.hpp"

#include "Document.hpp"
#include "PaUtil.hpp"
#include "gui/VuMeter.hpp"
#include "utils/VariantUtils.hpp"

#include <portaudio.h>

#include <algorithm>
#include <cmath>

#if CUPUACU_RTSAN_LIBS_ENABLED
#include <rtsan_standalone/rtsan_standalone.h>
#endif

using namespace cupuacu;
using namespace cupuacu::audio;
using namespace cupuacu::utils;

namespace
{
    constexpr int SAMPLE_RATE = 44100;
    constexpr unsigned long BUFFER_SIZE = 256;

    bool enqueueRecordedChunk(void *userdata,
                              const cupuacu::audio::RecordedChunk &chunk)
    {
        auto *queue =
            static_cast<moodycamel::ReaderWriterQueue<cupuacu::audio::RecordedChunk> *>(
                userdata);
        return queue->try_enqueue(chunk);
    }
} // namespace

AudioDevices::AudioDevices(const bool openDefaultDevice)
    : concurrency::AtomicStateExchange<AudioDeviceState, AudioDeviceView,
                                       AudioMessage>([](AudioDeviceState &) {})
{
    PaError err = Pa_Initialize();

    if (err != paNoError)
    {
        cupuacu::PaUtil::handlePaError(err);
        return;
    }

    DeviceSelection initialSelection{};
    const PaDeviceIndex defaultOutput = Pa_GetDefaultOutputDevice();
    if (defaultOutput != paNoDevice)
    {
        initialSelection.outputDeviceIndex = defaultOutput;
        const PaDeviceInfo *outputInfo = Pa_GetDeviceInfo(defaultOutput);
        if (outputInfo)
        {
            initialSelection.hostApiIndex = outputInfo->hostApi;
        }
    }

    const PaDeviceIndex defaultInput = Pa_GetDefaultInputDevice();
    if (defaultInput != paNoDevice)
    {
        initialSelection.inputDeviceIndex = defaultInput;
        if (initialSelection.hostApiIndex < 0)
        {
            const PaDeviceInfo *inputInfo = Pa_GetDeviceInfo(defaultInput);
            if (inputInfo)
            {
                initialSelection.hostApiIndex = inputInfo->hostApi;
            }
        }
    }

    {
        std::lock_guard<std::mutex> lock(selectionMutex);
        deviceSelection = initialSelection;
    }

    if (openDefaultDevice && initialSelection.outputDeviceIndex >= 0)
    {
        openDevice(initialSelection.inputDeviceIndex,
                   initialSelection.outputDeviceIndex);
    }
}

AudioDevices::~AudioDevices()
{
    closeDevice();
    Pa_Terminate();
}

void AudioDevices::writeSilenceToOutput(float *out, const unsigned long frames)
{
    callback_core::writeSilenceToOutput(out, frames);
}

bool AudioDevices::fillOutputBuffer(PaData &data, float *out,
                                    const unsigned long framesPerBuffer,
                                    float &peakLeft, float &peakRight)
{
    AudioDeviceState *state = &data.device->activeState;
    const bool playedAnyFrame = callback_core::fillOutputBuffer(
        data.playbackDocument, data.selectionIsActive, data.selectedChannels,
        state->playbackPosition, data.playbackEndPos, state->isPlaying, out,
        framesPerBuffer, peakLeft, peakRight);
    if (!state->isPlaying)
    {
        data.playbackDocument = nullptr;
    }
    return playedAnyFrame;
}

void AudioDevices::recordInputIntoQueue(PaData &data, const float *input,
                                        const unsigned long framesPerBuffer,
                                        float &peakLeft, float &peakRight)
{
    AudioDeviceState *state = &data.device->activeState;
    if (!state->isRecording || !data.recordingDocument || !input)
    {
        return;
    }

    int recordingChannels = data.recordingChannelCount;
    if (recordingChannels <= 0)
    {
        recordingChannels = static_cast<int>(std::clamp<int64_t>(
            data.recordingDocument->getChannelCount(), 1, 2));
    }
    if (recordingChannels <= 0)
    {
        return;
    }
    callback_core::recordInputIntoChunks(
        input, framesPerBuffer, static_cast<uint8_t>(recordingChannels),
        state->recordingPosition,
        static_cast<void *>(&data.device->recordedChunkQueue),
        enqueueRecordedChunk, peakLeft, peakRight);
}

void AudioDevices::pushPeaksToVuMeter(PaData &data, const float peakLeft,
                                      const float peakRight,
                                      const bool isPlaying,
                                      const bool isRecording)
{
    if (!data.vuMeter)
    {
        return;
    }

    if (isPlaying || isRecording)
    {
        data.vuMeter->pushPeakForChannel(peakLeft, 0);
        data.vuMeter->pushPeakForChannel(peakRight, 1);
        data.vuMeter->setPeaksPushed();
        return;
    }

    data.vuMeter->startDecay();
}

int AudioDevices::paCallback(const void *inputBuffer, void *outputBuffer,
                             const unsigned long framesPerBuffer,
                             const PaStreamCallbackTimeInfo *timeInfo,
                             PaStreamCallbackFlags statusFlags, void *userData)
{
#if CUPUACU_RTSAN_LIBS_ENABLED
    __rtsan::ScopedSanitizeRealtime realtimeScope;
#endif

    (void)timeInfo;
    (void)statusFlags;

    auto *data = static_cast<PaData *>(userData);
    return data->device->processCallbackCycle(
        static_cast<const float *>(inputBuffer), outputBuffer, framesPerBuffer);
}

int AudioDevices::processCallbackCycle(const float *inputBuffer, void *outputBuffer,
                                       const unsigned long framesPerBuffer) noexcept
{
    drainQueue();

    float peakLeft = 0.0f;
    float peakRight = 0.0f;

    const bool isPlaying =
        fillOutputBuffer(paData, static_cast<float *>(outputBuffer),
                         framesPerBuffer, peakLeft, peakRight);
    recordInputIntoQueue(paData, inputBuffer, framesPerBuffer, peakLeft,
                         peakRight);

    pushPeaksToVuMeter(paData, peakLeft, peakRight, isPlaying,
                       activeState.isRecording);

    publishState();
    return 0;
}

void AudioDevices::openDevice(const int inputDeviceIndex,
                              const int outputDeviceIndex)
{
    std::lock_guard<std::mutex> lock(streamMutex);

    const bool outputChanged = outputDeviceIndex != currentOutputDeviceIndex;
    const bool inputChanged = inputDeviceIndex != currentInputDeviceIndex;
    currentInputDeviceIndex = inputDeviceIndex;
    currentOutputDeviceIndex = outputDeviceIndex;

    if (!outputChanged && !inputChanged && stream)
    {
        return;
    }

    closeDeviceLocked();

    if (outputDeviceIndex < 0)
    {
        return;
    }

    const PaDeviceInfo *outputInfo = Pa_GetDeviceInfo(outputDeviceIndex);
    if (!outputInfo)
    {
        return;
    }

    PaStreamParameters outputParameters{};
    outputParameters.device = outputDeviceIndex;
    outputParameters.channelCount = 2;
    outputParameters.sampleFormat = paFloat32;
    outputParameters.suggestedLatency = outputInfo->defaultLowOutputLatency;
    outputParameters.hostApiSpecificStreamInfo = nullptr;

    PaStreamParameters inputParameters{};
    PaStreamParameters *inputParametersPtr = nullptr;
    paData.recordingChannelCount = 0;
    if (inputDeviceIndex >= 0)
    {
        const PaDeviceInfo *inputInfo = Pa_GetDeviceInfo(inputDeviceIndex);
        if (inputInfo && inputInfo->maxInputChannels > 0)
        {
            inputParameters.device = inputDeviceIndex;
            inputParameters.channelCount =
                std::clamp(inputInfo->maxInputChannels, 1, 2);
            inputParameters.sampleFormat = paFloat32;
            inputParameters.suggestedLatency =
                inputInfo->defaultLowInputLatency;
            inputParameters.hostApiSpecificStreamInfo = nullptr;
            inputParametersPtr = &inputParameters;
            paData.recordingChannelCount =
                static_cast<uint8_t>(inputParameters.channelCount);
        }
    }

    paData.device = this;

    PaError err =
        Pa_OpenStream(&stream, inputParametersPtr, &outputParameters,
                      SAMPLE_RATE, BUFFER_SIZE, paNoFlag, paCallback, &paData);
    if (err != paNoError)
    {
        PaUtil::handlePaError(err);
        stream = nullptr;
        return;
    }

    err = Pa_StartStream(stream);
    if (err != paNoError)
    {
        PaUtil::handlePaError(err);
        Pa_CloseStream(stream);
        stream = nullptr;
    }
}

void AudioDevices::closeDevice()
{
    std::lock_guard<std::mutex> lock(streamMutex);
    closeDeviceLocked();
    currentInputDeviceIndex = -1;
    currentOutputDeviceIndex = -1;
    paData.recordingChannelCount = 0;
}

void AudioDevices::closeDeviceLocked()
{
    if (!stream)
    {
        return;
    }

    PaError err = Pa_StopStream(stream);
    if (err != paNoError && err != paStreamIsStopped)
    {
        PaUtil::handlePaError(err);
    }

    err = Pa_CloseStream(stream);
    if (err != paNoError)
    {
        PaUtil::handlePaError(err);
    }
    stream = nullptr;
}

void AudioDevices::applyMessage(const AudioMessage &msg) noexcept
{
    auto visitor = Overload{[&](const Play &m)
                            {
                                paData.playbackDocument = m.document;
                                paData.device = this;
                                activeState.playbackPosition = m.startPos;
                                paData.playbackEndPos = m.endPos;
                                activeState.isPlaying = true;
                                paData.selectedChannels = m.selectedChannels;
                                paData.selectionIsActive = m.selectionIsActive;
                                paData.vuMeter = m.vuMeter;
                            },
                            [&](const Stop &)
                            {
                                paData.playbackDocument = nullptr;
                                paData.recordingDocument = nullptr;
                                activeState.playbackPosition = -1;
                                activeState.recordingPosition = -1;
                                activeState.isPlaying = false;
                                activeState.isRecording = false;
                            },
                            [&](const Record &m)
                            {
                                paData.recordingDocument = m.document;
                                paData.device = this;
                                activeState.recordingPosition = m.startPos;
                                activeState.isRecording = true;
                                paData.vuMeter = m.vuMeter;
                            }};

    std::visit(visitor, msg);
}

bool AudioDevices::isPlaying() const
{
    return getSnapshot().isPlaying();
}

bool AudioDevices::isRecording() const
{
    return getSnapshot().isRecording();
}

int64_t AudioDevices::getPlaybackPosition() const
{
    return getSnapshot().getPlaybackPosition();
}

int64_t AudioDevices::getRecordingPosition() const
{
    return getSnapshot().getRecordingPosition();
}

bool AudioDevices::popRecordedChunk(RecordedChunk &outChunk)
{
    return recordedChunkQueue.try_dequeue(outChunk);
}

void AudioDevices::clearRecordedChunks()
{
    RecordedChunk chunk{};
    while (recordedChunkQueue.try_dequeue(chunk))
    {
    }
}

AudioDevices::DeviceSelection AudioDevices::getDeviceSelection() const
{
    std::lock_guard<std::mutex> lock(selectionMutex);
    return deviceSelection;
}

bool AudioDevices::setDeviceSelection(const DeviceSelection &selection)
{
    {
        std::lock_guard<std::mutex> lock(selectionMutex);
        if (selection == deviceSelection)
        {
            return false;
        }
        deviceSelection = selection;
    }

    openDevice(selection.inputDeviceIndex, selection.outputDeviceIndex);
    return true;
}
