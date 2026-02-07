#include "audio/AudioDevices.hpp"

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

    int getRecordingChannelCount(const int inputDeviceIndex)
    {
        if (inputDeviceIndex < 0)
        {
            return 0;
        }

        const PaDeviceInfo *inputInfo = Pa_GetDeviceInfo(inputDeviceIndex);
        if (!inputInfo)
        {
            return 0;
        }

        return std::clamp(inputInfo->maxInputChannels, 0, 2);
    }
} // namespace

AudioDevices::AudioDevices()
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

    if (initialSelection.outputDeviceIndex >= 0)
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
    if (!out)
    {
        return;
    }

    for (unsigned long i = 0; i < frames; ++i)
    {
        *out++ = 0.0f;
        *out++ = 0.0f;
    }
}

bool AudioDevices::fillOutputBuffer(PaData &data, float *out,
                                    const unsigned long framesPerBuffer,
                                    float &peakLeft, float &peakRight)
{
    if (!out)
    {
        return false;
    }

    AudioDeviceState *state = &data.device->activeState;
    const Document *document = data.playbackDocument;

    if (!document ||
        (document->getChannelCount() != 1 && document->getChannelCount() != 2))
    {
        writeSilenceToOutput(out, framesPerBuffer);
        return false;
    }

    const auto docBuf = document->getAudioBuffer();
    const auto chBufL = docBuf->getImmutableChannelData(0);
    const auto chBufR =
        docBuf->getImmutableChannelData(docBuf->getChannelCount() == 2 ? 1 : 0);

    const bool selectionIsActive = data.selectionIsActive;
    const auto selectedChannels = data.selectedChannels;

    const bool shouldPlayChannelL =
        !selectionIsActive || selectedChannels == SelectedChannels::BOTH ||
        selectedChannels == SelectedChannels::LEFT;

    const bool shouldPlayChannelR =
        !selectionIsActive || selectedChannels == SelectedChannels::BOTH ||
        selectedChannels == SelectedChannels::RIGHT;

    bool playedAnyFrame = false;

    for (unsigned long i = 0; i < framesPerBuffer; ++i)
    {
        if (state->playbackPosition >=
            static_cast<int64_t>(data.playbackEndPos))
        {
            data.playbackDocument = nullptr;
            state->isPlaying = false;
            state->playbackPosition = -1;
            *out++ = 0.f;
            *out++ = 0.f;
            continue;
        }

        const float outL =
            shouldPlayChannelL ? chBufL[state->playbackPosition] : 0.0f;
        const float outR =
            shouldPlayChannelR ? chBufR[state->playbackPosition] : 0.0f;

        *out++ = outL;
        *out++ = outR;

        peakLeft = std::max(peakLeft, std::abs(outL));
        peakRight = std::max(peakRight, std::abs(outR));
        ++state->playbackPosition;
        playedAnyFrame = true;
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

    const int recordingChannels =
        getRecordingChannelCount(data.device->currentInputDeviceIndex);
    if (recordingChannels <= 0)
    {
        return;
    }

    unsigned long frameOffset = 0;
    while (frameOffset < framesPerBuffer)
    {
        RecordedChunk chunk{};
        chunk.startFrame = state->recordingPosition;
        chunk.channelCount = static_cast<uint8_t>(recordingChannels);
        chunk.frameCount = static_cast<uint32_t>(std::min<unsigned long>(
            kRecordedChunkFrames, framesPerBuffer - frameOffset));

        for (uint32_t frame = 0; frame < chunk.frameCount; ++frame)
        {
            const std::size_t sourceBase =
                static_cast<std::size_t>(frameOffset + frame) *
                static_cast<std::size_t>(recordingChannels);
            const float inL = input[sourceBase];
            const float inR =
                recordingChannels > 1 ? input[sourceBase + 1] : inL;

            const std::size_t targetBase =
                static_cast<std::size_t>(frame) * kMaxRecordedChannels;
            chunk.interleavedSamples[targetBase] = inL;
            chunk.interleavedSamples[targetBase + 1] = inR;

            peakLeft = std::max(peakLeft, std::abs(inL));
            peakRight = std::max(peakRight, std::abs(inR));
        }

        data.device->recordedChunkQueue.try_enqueue(chunk);
        state->recordingPosition += static_cast<int64_t>(chunk.frameCount);
        frameOffset += chunk.frameCount;
    }
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
    data->device->drainQueue();

    float peakLeft = 0.0f;
    float peakRight = 0.0f;

    const bool isPlaying =
        fillOutputBuffer(*data, static_cast<float *>(outputBuffer),
                         framesPerBuffer, peakLeft, peakRight);
    recordInputIntoQueue(*data, static_cast<const float *>(inputBuffer),
                         framesPerBuffer, peakLeft, peakRight);

    pushPeaksToVuMeter(*data, peakLeft, peakRight, isPlaying,
                       data->device->activeState.isRecording);

    data->device->publishState();
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
