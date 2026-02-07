#include "AudioDevice.hpp"

#include "PaUtil.hpp"
#include "Document.hpp"
#include "concurrency/AtomicStateExchange.hpp"

#include "utils/VariantUtils.hpp"
#include "gui/VuMeter.hpp"
#include "State.hpp"

#include <portaudio.h>

using namespace cupuacu;
using namespace cupuacu::utils;

typedef struct PaData
{
    Document *document = nullptr;
    bool selectionIsActive = false;
    SelectedChannels selectedChannels;
    AudioDevice *device;
    uint64_t endPos;
    gui::VuMeter *vuMeter;
} paData;

int AudioDevice::paCallback(const void *inputBuffer, void *outputBuffer,
                            unsigned long framesPerBuffer,
                            const PaStreamCallbackTimeInfo *timeInfo,
                            PaStreamCallbackFlags statusFlags, void *userData)
{
    paData *data = (paData *)userData;

    data->device->drainQueue();

    float *out = (float *)outputBuffer;
    unsigned int i;
    (void)inputBuffer;

    if (!data->document || (data->document->getChannelCount() != 1 &&
                            data->document->getChannelCount() != 2))
    {
        for (i = 0; i < framesPerBuffer; i++)
        {
            *out++ = 0.f;
            *out++ = 0.f;
        }
        return 0;
    }

    const auto docBuf = data->document->getAudioBuffer();
    const auto chBufL = docBuf->getImmutableChannelData(0);
    const auto chBufR =
        docBuf->getImmutableChannelData(docBuf->getChannelCount() == 2 ? 1 : 0);

    AudioDeviceState *state = &data->device->activeState;

    float peaks[2] = {0.f, 0.f};

    const bool selectionIsActive = data->selectionIsActive;
    const auto selectedChannels = data->selectedChannels;

    const bool shouldPlayChannelL =
        !selectionIsActive ||
        selectedChannels == cupuacu::SelectedChannels::BOTH ||
        selectedChannels == cupuacu::SelectedChannels::LEFT;

    const bool shouldPlayChannelR =
        !selectionIsActive ||
        selectedChannels == cupuacu::SelectedChannels::BOTH ||
        selectedChannels == cupuacu::SelectedChannels::RIGHT;

    bool donePlaying = false;

    for (i = 0; i < framesPerBuffer; ++i)
    {
        if (state->playbackPosition >= data->endPos)
        {
            if (!donePlaying)
            {
                data->document = nullptr;
                state->isPlaying = false;
                state->playbackPosition = -1;
                donePlaying = true;
            }
            *out++ = 0.f;
            *out++ = 0.f;
            continue;
        }

        *out = shouldPlayChannelL ? chBufL[state->playbackPosition] : 0.f;
        peaks[0] = std::max(peaks[0], std::abs(*out));
        ++out;

        *out = shouldPlayChannelR ? chBufR[state->playbackPosition] : 0.f;
        peaks[1] = std::max(peaks[0], std::abs(*out));
        ++out;

        ++state->playbackPosition;
    }

    if (donePlaying)
    {
        data->vuMeter->startDecay();
    }
    else
    {
        data->vuMeter->pushPeakForChannel(peaks[0], 0);

        if (docBuf->getChannelCount() == 2)
        {
            data->vuMeter->pushPeakForChannel(peaks[1], 1);
        }

        data->vuMeter->setPeaksPushed();
    }

    data->device->publishState();

    return 0;
}

static paData data;

const int SAMPLE_RATE = 44100;

AudioDevice::AudioDevice()
    : concurrency::AtomicStateExchange<AudioDeviceState, AudioDeviceView,
                                       AudioMessage>([](AudioDeviceState &) {})
{
}

AudioDevice::~AudioDevice()
{
    closeDevice();
}

void AudioDevice::openDevice(const int inputDeviceIndex,
                             const int outputDeviceIndex)
{
    std::lock_guard<std::mutex> lock(streamMutex);

    const bool outputChanged = outputDeviceIndex != currentOutputDeviceIndex;
    currentInputDeviceIndex = inputDeviceIndex;
    currentOutputDeviceIndex = outputDeviceIndex;

    if (!outputChanged && stream)
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

    PaError err = Pa_OpenStream(&stream, nullptr, &outputParameters,
                                SAMPLE_RATE, 256, paNoFlag, paCallback, &data);
    if (err != paNoError)
    {
        PaUtil::handlePaError(err);
        stream = nullptr;
        return;
    }

    data.device = this;

    err = Pa_StartStream(stream);
    if (err != paNoError)
    {
        PaUtil::handlePaError(err);
        Pa_CloseStream(stream);
        stream = nullptr;
        return;
    }
}

void AudioDevice::closeDevice()
{
    std::lock_guard<std::mutex> lock(streamMutex);
    closeDeviceLocked();
    currentInputDeviceIndex = -1;
    currentOutputDeviceIndex = -1;
}

void AudioDevice::closeDeviceLocked()
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

void AudioDevice::applyMessage(const AudioMessage &msg) noexcept
{
    auto visitor = Overload{[&](const Play &m)
                            {
                                data.document = m.document;
                                data.device = this;
                                activeState.playbackPosition = m.startPos;
                                data.endPos = m.endPos;
                                activeState.isPlaying = true;
                                data.selectedChannels = m.selectedChannels;
                                data.selectionIsActive = m.selectionIsActive;
                                data.vuMeter = m.vuMeter;
                            },
                            [&](const Stop &)
                            {
                                data.document = nullptr;
                                activeState.playbackPosition = -1;
                                activeState.isPlaying = false;
                            }};

    std::visit(visitor, msg);
}

bool AudioDevice::isPlaying() const
{
    return getSnapshot().isPlaying();
}

int64_t AudioDevice::getPlaybackPosition() const
{
    return getSnapshot().getPlaybackPosition();
}
