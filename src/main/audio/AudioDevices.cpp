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
        auto *queue = static_cast<
            moodycamel::ReaderWriterQueue<cupuacu::audio::RecordedChunk> *>(
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
        openDevice(-1, initialSelection.outputDeviceIndex);
    }
}

AudioDevices::~AudioDevices()
{
    closeDevice();
    Pa_Terminate();
}

void AudioDevices::enqueue(Play msg) noexcept
{
    snapshotQueuedPlayMessage(msg);
    Base::enqueue(std::move(msg));
}

void AudioDevices::enqueue(Record msg) noexcept
{
    snapshotQueuedRecordMessage(msg);
    Base::enqueue(std::move(msg));
}

void AudioDevices::writeSilenceToOutput(float *out, const unsigned long frames)
{
    callback_core::writeSilenceToOutput(out, frames);
}

bool AudioDevices::fillOutputBuffer(
    PaData &data, float *out, const unsigned long framesPerBuffer,
    callback_core::StereoMeterLevels &meterLevels)
{
    AudioDeviceState *state = &data.device->activeState;
    const bool playedAnyFrame = callback_core::fillOutputBuffer(
        data.playbackBuffer, data.playbackChannelCount, data.selectionIsActive,
        data.selectedChannels, state->playbackPosition, data.playbackStartPos,
        data.playbackEndPos, data.playbackLoopEnabled,
        data.playbackHasPendingSwitch, data.playbackPendingStartPos,
        data.playbackPendingEndPos, state->isPlaying, out, framesPerBuffer,
        meterLevels, data.previewProcessor.get(), data.playbackStartPos,
        data.playbackEndPos, data.selectedChannels);
    if (!state->isPlaying)
    {
        data.playbackBuffer.reset();
        data.playbackChannelCount = 0;
        data.previewProcessor.reset();
    }
    return playedAnyFrame;
}

void AudioDevices::recordInputIntoQueue(
    PaData &data, const float *input, const unsigned long framesPerBuffer,
    callback_core::StereoMeterLevels &meterLevels)
{
    AudioDeviceState *state = &data.device->activeState;
    if (!state->isRecording || !input)
    {
        return;
    }

    int recordingChannels = data.inputChannelCount;
    if (recordingChannels <= 0)
    {
        recordingChannels = static_cast<int>(
            std::max<uint8_t>(data.recordingDocumentChannelCount, uint8_t{1}));
    }
    if (recordingChannels <= 0)
    {
        return;
    }

    unsigned long framesToRecord = framesPerBuffer;
    if (data.recordingBoundedToEnd)
    {
        if (state->recordingPosition >=
            static_cast<int64_t>(data.recordingEndPos))
        {
            state->isRecording = false;
            return;
        }
        const uint64_t remaining =
            data.recordingEndPos -
            static_cast<uint64_t>(state->recordingPosition);
        framesToRecord = std::min<unsigned long>(
            framesPerBuffer, static_cast<unsigned long>(remaining));
    }

    if (framesToRecord == 0)
    {
        state->isRecording = false;
        return;
    }

    if (!callback_core::recordInputIntoChunks(
            input, framesToRecord, static_cast<uint8_t>(recordingChannels),
            state->recordingPosition,
            static_cast<void *>(&data.device->recordedChunkQueue),
            enqueueRecordedChunk, meterLevels))
    {
        data.device->recordingOverflowed.store(true,
                                               std::memory_order_release);
        state->isRecording = false;
        data.recordingBoundedToEnd = false;
    }

    if (data.recordingBoundedToEnd &&
        state->recordingPosition >= static_cast<int64_t>(data.recordingEndPos))
    {
        state->isRecording = false;
    }
}

void AudioDevices::pushPeaksToVuMeter(
    PaData &data, const callback_core::StereoMeterLevels &meterLevels,
    const bool isPlaying, const bool isRecording, const bool isMonitoring)
{
    if (!data.vuMeter)
    {
        return;
    }

    if (isPlaying || isRecording || isMonitoring)
    {
        data.vuMeter->pushMeterFrameForChannel(
            {.peak = meterLevels.peakLeft, .rms = meterLevels.rmsLeft}, 0);
        data.vuMeter->pushMeterFrameForChannel(
            {.peak = meterLevels.peakRight, .rms = meterLevels.rmsRight}, 1);
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

    auto *data = static_cast<PaData *>(userData);
    AudioCallbackTiming timing{};
    if (timeInfo)
    {
        timing.inputAdcTime = timeInfo->inputBufferAdcTime;
        timing.currentTime = timeInfo->currentTime;
        timing.outputDacTime = timeInfo->outputBufferDacTime;
        timing.valid = true;
    }
    timing.discontinuity =
        (statusFlags & (paInputOverflow | paInputUnderflow | paOutputOverflow |
                        paOutputUnderflow)) != 0;
    return data->device->processCallbackCycle(
        static_cast<const float *>(inputBuffer), outputBuffer, framesPerBuffer,
        timing);
}

int AudioDevices::processCallbackCycle(
    const float *inputBuffer, void *outputBuffer,
    const unsigned long framesPerBuffer,
    const AudioCallbackTiming &timing) noexcept
{
    drainQueue();

    callback_core::StereoMeterLevels meterLevels{};

    const bool playedAnyFrame =
        fillOutputBuffer(paData, static_cast<float *>(outputBuffer),
                         framesPerBuffer, meterLevels);
    recordInputIntoQueue(paData, inputBuffer, framesPerBuffer, meterLevels);

    const bool playbackOwnsOutput = playedAnyFrame || activeState.isPlaying;
    bool monitoredAnyFrame = false;
    if (activeState.isInputMonitoringEnabled && playbackOwnsOutput)
    {
        if (monitorPipeline && !paData.monitorWasSuspendedForPlayback)
        {
            monitorPipeline->suspendSession();
        }
        paData.monitorWasSuspendedForPlayback = true;
        activeState.monitorProtection.state = MonitorProtectionState::Inactive;
    }
    else if (activeState.isInputMonitoringEnabled && monitorPipeline)
    {
        if (paData.monitorWasSuspendedForPlayback)
        {
            monitorPipeline->startSession();
            paData.monitorWasSuspendedForPlayback = false;
        }
        if (!activeState.isRecording)
        {
            callback_core::measureInput(inputBuffer, paData.inputChannelCount,
                                        framesPerBuffer, meterLevels);
        }

        const auto monitorResult = monitorPipeline->process(
            inputBuffer, static_cast<float *>(outputBuffer), framesPerBuffer,
            timing);
        activeState.monitorProtection = monitorResult.telemetry;
        activeState.monitorProtection.tripGeneration = monitorTripGeneration;
        monitoredAnyFrame = true;

        if (monitorResult.telemetry.state == MonitorProtectionState::Tripped ||
            monitorResult.telemetry.state ==
                MonitorProtectionState::Unavailable)
        {
            activeState.monitorProtection.tripGeneration =
                ++monitorTripGeneration;
            activeState.isInputMonitoringEnabled = false;
            inputMonitoringRequested.store(false, std::memory_order_release);
            inputMonitoringError.store(
                monitorResult.telemetry.state ==
                        MonitorProtectionState::Unavailable
                    ? InputMonitoringError::SuppressionUnavailable
                    : InputMonitoringError::None,
                std::memory_order_release);
            monitoredAnyFrame = false;
        }
    }
    else
    {
        paData.monitorWasSuspendedForPlayback = false;
    }

    pushPeaksToVuMeter(paData, meterLevels, playedAnyFrame,
                       activeState.isRecording, monitoredAnyFrame);

    publishState();
    return 0;
}

bool AudioDevices::openDevice(const int inputDeviceIndex,
                              const int outputDeviceIndex)
{
    std::lock_guard<std::mutex> lock(streamMutex);

    const bool outputChanged = outputDeviceIndex != currentOutputDeviceIndex;
    const bool inputChanged = inputDeviceIndex != currentInputDeviceIndex;
    currentInputDeviceIndex = inputDeviceIndex;
    currentOutputDeviceIndex = outputDeviceIndex;

    if (!outputChanged && !inputChanged && stream)
    {
        return true;
    }

    closeDeviceLocked();

    if (outputDeviceIndex < 0)
    {
        return false;
    }

    const PaDeviceInfo *outputInfo = Pa_GetDeviceInfo(outputDeviceIndex);
    if (!outputInfo)
    {
        return false;
    }

    PaStreamParameters outputParameters{};
    outputParameters.device = outputDeviceIndex;
    outputParameters.channelCount = 2;
    outputParameters.sampleFormat = paFloat32;
    outputParameters.suggestedLatency = outputInfo->defaultLowOutputLatency;
    outputParameters.hostApiSpecificStreamInfo = nullptr;

    PaStreamParameters inputParameters{};
    PaStreamParameters *inputParametersPtr = nullptr;
    paData.inputChannelCount = 0;
    if (inputDeviceIndex >= 0)
    {
        const PaDeviceInfo *inputInfo = Pa_GetDeviceInfo(inputDeviceIndex);
        if (!inputInfo || inputInfo->maxInputChannels <= 0)
        {
            return false;
        }
        inputParameters.device = inputDeviceIndex;
        inputParameters.channelCount =
            std::clamp(inputInfo->maxInputChannels, 1, 2);
        inputParameters.sampleFormat = paFloat32;
        inputParameters.suggestedLatency = inputInfo->defaultLowInputLatency;
        inputParameters.hostApiSpecificStreamInfo = nullptr;
        inputParametersPtr = &inputParameters;
        paData.inputChannelCount =
            static_cast<uint8_t>(inputParameters.channelCount);
    }

    paData.device = this;

    PaError err =
        Pa_OpenStream(&stream, inputParametersPtr, &outputParameters,
                      SAMPLE_RATE, BUFFER_SIZE, paNoFlag, paCallback, &paData);
    if (err != paNoError)
    {
        PaUtil::handlePaError(err);
        stream = nullptr;
        return false;
    }

    monitorPipeline.reset();
    if (inputParametersPtr)
    {
        const PaStreamInfo *streamInfo = Pa_GetStreamInfo(stream);
        MonitorStreamFormat monitorFormat{
            .sampleRate = streamInfo ? static_cast<int>(
                                           std::lround(streamInfo->sampleRate))
                                     : SAMPLE_RATE,
            .callbackFrames = BUFFER_SIZE,
            .inputChannels = paData.inputChannelCount,
            .outputChannels = 2,
            .inputLatencySeconds = streamInfo
                                       ? streamInfo->inputLatency
                                       : inputParameters.suggestedLatency,
            .outputLatencySeconds = streamInfo
                                        ? streamInfo->outputLatency
                                        : outputParameters.suggestedLatency};
        auto candidate = std::make_unique<InputMonitorPipeline>();
        if (candidate->prepare(monitorFormat))
        {
            candidate->setFeedbackSuppressionMode(
                feedbackSuppressionMode.load(std::memory_order_acquire));
            monitorPipeline = std::move(candidate);
        }
    }

    err = Pa_StartStream(stream);
    if (err != paNoError)
    {
        PaUtil::handlePaError(err);
        Pa_CloseStream(stream);
        stream = nullptr;
        monitorPipeline.reset();
        return false;
    }
    return true;
}

void AudioDevices::closeDevice()
{
    std::lock_guard<std::mutex> lock(streamMutex);
    closeDeviceLocked();
    currentInputDeviceIndex = -1;
    currentOutputDeviceIndex = -1;
    paData.inputChannelCount = 0;
}

bool AudioDevices::prepareForRecording()
{
    if (recordingPreparationResultForTesting)
    {
        return *recordingPreparationResultForTesting;
    }

    const auto selection = getDeviceSelection();
    if (selection.inputDeviceIndex < 0 || selection.outputDeviceIndex < 0)
    {
        return false;
    }
    return openDevice(selection.inputDeviceIndex, selection.outputDeviceIndex);
}

void AudioDevices::setRecordingPreparationResultForTesting(const bool result)
{
    recordingPreparationResultForTesting = result;
}

bool AudioDevices::setInputMonitoringEnabled(const bool enabled,
                                             gui::VuMeter *vuMeter)
{
    if (enabled)
    {
        if (isPlaying())
        {
            return false;
        }

        if (!isRecording())
        {
            const auto selection = getDeviceSelection();
            if (selection.inputDeviceIndex < 0 ||
                selection.outputDeviceIndex < 0 ||
                !openDevice(selection.inputDeviceIndex,
                            selection.outputDeviceIndex))
            {
                inputMonitoringError.store(
                    InputMonitoringError::DeviceUnavailable,
                    std::memory_order_release);
                return false;
            }
        }
        const auto mode =
            feedbackSuppressionMode.load(std::memory_order_acquire);
        if (!monitorPipeline || !monitorPipeline->isPrepared() ||
            !monitorPipeline->isModeAvailable(mode))
        {
            inputMonitoringError.store(
                InputMonitoringError::SuppressionUnavailable,
                std::memory_order_release);
            return false;
        }
    }

    inputMonitoringError.store(InputMonitoringError::None,
                               std::memory_order_release);
    inputMonitoringRequested.store(enabled, std::memory_order_release);
    Base::enqueue(
        SetInputMonitoring{.enabled = enabled,
                           .inputChannelCount = paData.inputChannelCount,
                           .vuMeter = vuMeter});

    if (!enabled && !isPlaying() && !isRecording())
    {
        const auto selection = getDeviceSelection();
        openDevice(-1, selection.outputDeviceIndex);
    }
    return true;
}

bool AudioDevices::isInputMonitoringEnabled() const noexcept
{
    return inputMonitoringRequested.load(std::memory_order_acquire);
}

InputMonitoringError AudioDevices::getInputMonitoringError() const noexcept
{
    return inputMonitoringError.load(std::memory_order_acquire);
}

MonitorProtectionTelemetry AudioDevices::getMonitorProtectionTelemetry() const
{
    return getSnapshot().getMonitorProtectionTelemetry();
}

FeedbackSuppressionMode
AudioDevices::getFeedbackSuppressionMode() const noexcept
{
    return feedbackSuppressionMode.load(std::memory_order_acquire);
}

bool AudioDevices::setFeedbackSuppressionMode(
    const FeedbackSuppressionMode mode) noexcept
{
    const auto previous =
        feedbackSuppressionMode.exchange(mode, std::memory_order_acq_rel);
    if (previous == mode)
    {
        return false;
    }
    inputMonitoringError.store(InputMonitoringError::None,
                               std::memory_order_release);
    Base::enqueue(SetFeedbackSuppressionMode{.mode = mode});
    return true;
}

void AudioDevices::releaseInputIfUnused()
{
    if (isInputMonitoringEnabled() || isPlaying() || isRecording())
    {
        return;
    }
    const auto selection = getDeviceSelection();
    openDevice(-1, selection.outputDeviceIndex);
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
    monitorPipeline.reset();
}

void AudioDevices::snapshotQueuedPlayMessage(Play &msg)
{
    if (!msg.document)
    {
        msg.bufferSnapshot.reset();
        msg.channelCountSnapshot = 0;
        return;
    }

    msg.bufferSnapshot = msg.document->getAudioBuffer();
    msg.channelCountSnapshot = static_cast<uint8_t>(
        std::clamp<int64_t>(msg.document->getChannelCount(), 0, 2));
}

void AudioDevices::snapshotQueuedRecordMessage(Record &msg)
{
    if (!msg.document)
    {
        msg.channelCountSnapshot = 0;
        return;
    }

    msg.channelCountSnapshot = static_cast<uint8_t>(
        std::clamp<int64_t>(msg.document->getChannelCount(), 0, 2));
}

void AudioDevices::applyMessage(const AudioMessage &msg) noexcept
{
    auto visitor = Overload{
        [&](const Play &m)
        {
            paData.playbackBuffer = m.bufferSnapshot;
            paData.playbackChannelCount = m.channelCountSnapshot;
            if (!paData.playbackBuffer && m.document)
            {
                paData.playbackBuffer = m.document->getAudioBuffer();
            }
            if (paData.playbackChannelCount == 0 && m.document)
            {
                paData.playbackChannelCount = static_cast<uint8_t>(
                    std::clamp<int64_t>(m.document->getChannelCount(), 0, 2));
            }
            paData.device = this;
            activeState.playbackPosition = m.startPos;
            paData.playbackStartPos = m.startPos;
            paData.playbackEndPos = m.endPos;
            paData.playbackLoopEnabled = m.loopEnabled;
            paData.playbackHasPendingSwitch = false;
            activeState.isPlaying = true;
            paData.selectedChannels = m.selectedChannels;
            paData.selectionIsActive = m.selectionIsActive;
            paData.vuMeter = m.vuMeter;
            paData.previewProcessor = m.previewProcessor;
        },
        [&](const Stop &)
        {
            paData.playbackBuffer.reset();
            paData.playbackChannelCount = 0;
            paData.playbackLoopEnabled = false;
            paData.playbackHasPendingSwitch = false;
            paData.previewProcessor.reset();
            paData.recordingBoundedToEnd = false;
            paData.recordingDocumentChannelCount = 0;
            activeState.playbackPosition = -1;
            activeState.recordingPosition = -1;
            activeState.isPlaying = false;
            activeState.isRecording = false;
        },
        [&](const SetInputMonitoring &m)
        {
            paData.device = this;
            activeState.isInputMonitoringEnabled = m.enabled;
            if (m.enabled)
            {
                if (m.inputChannelCount > 0)
                {
                    paData.inputChannelCount = m.inputChannelCount;
                }
                paData.vuMeter = m.vuMeter;
                paData.monitorWasSuspendedForPlayback = false;
                if (monitorPipeline)
                {
                    monitorPipeline->startSession();
                    activeState.monitorProtection =
                        monitorPipeline->getTelemetry();
                    activeState.monitorProtection.tripGeneration =
                        monitorTripGeneration;
                }
            }
            else
            {
                paData.monitorWasSuspendedForPlayback = false;
                if (monitorPipeline)
                {
                    monitorPipeline->suspendSession();
                    activeState.monitorProtection =
                        monitorPipeline->getTelemetry();
                    activeState.monitorProtection.tripGeneration =
                        monitorTripGeneration;
                }
                else
                {
                    activeState.monitorProtection.state =
                        MonitorProtectionState::Inactive;
                }
            }
        },
        [&](const SetFeedbackSuppressionMode &m)
        {
            if (!monitorPipeline)
            {
                return;
            }
            if (!monitorPipeline->setFeedbackSuppressionMode(m.mode))
            {
                activeState.monitorProtection.state =
                    MonitorProtectionState::Unavailable;
                if (activeState.isInputMonitoringEnabled)
                {
                    activeState.isInputMonitoringEnabled = false;
                    inputMonitoringRequested.store(false,
                                                   std::memory_order_release);
                    inputMonitoringError.store(
                        InputMonitoringError::SuppressionUnavailable,
                        std::memory_order_release);
                }
                return;
            }
            activeState.monitorProtection = monitorPipeline->getTelemetry();
            activeState.monitorProtection.tripGeneration =
                monitorTripGeneration;
        },
        [&](const Record &m)
        {
            paData.device = this;
            activeState.recordingPosition = m.startPos;
            paData.recordingEndPos = m.endPos;
            paData.recordingBoundedToEnd = m.boundedToEnd;
            paData.recordingDocumentChannelCount = m.channelCountSnapshot;
            if (paData.recordingDocumentChannelCount == 0 && m.document)
            {
                paData.recordingDocumentChannelCount = static_cast<uint8_t>(
                    std::clamp<int64_t>(m.document->getChannelCount(), 0, 2));
            }
            activeState.isRecording = true;
            paData.vuMeter = m.vuMeter;
        },
        [&](const UpdatePlayback &m)
        {
            if (!activeState.isPlaying)
            {
                return;
            }
            paData.selectionIsActive = m.selectionIsActive;
            paData.selectedChannels = m.selectedChannels;

            if (m.endPos <= m.startPos)
            {
                paData.playbackBuffer.reset();
                paData.playbackChannelCount = 0;
                activeState.isPlaying = false;
                activeState.playbackPosition = -1;
                return;
            }

            const bool wasLooping = paData.playbackLoopEnabled;
            const bool wantsLooping = m.loopEnabled;
            paData.playbackLoopEnabled = wantsLooping;

            if (wasLooping && wantsLooping)
            {
                const int64_t currentPos = activeState.playbackPosition;
                const int64_t requestedEnd = static_cast<int64_t>(m.endPos);
                if (requestedEnd > currentPos)
                {
                    paData.playbackEndPos = m.endPos;
                }
                paData.playbackPendingStartPos = m.startPos;
                paData.playbackPendingEndPos = m.endPos;
                paData.playbackHasPendingSwitch = true;
                return;
            }

            paData.playbackStartPos = m.startPos;
            paData.playbackHasPendingSwitch = false;

            const int64_t currentPos = activeState.playbackPosition;
            const int64_t requestedEnd = static_cast<int64_t>(m.endPos);

            if (!wantsLooping)
            {
                // For non-loop playback:
                // if new end is before/equal current playback pos,
                // keep the old end; otherwise switch to new end.
                if (requestedEnd > currentPos)
                {
                    paData.playbackEndPos = m.endPos;
                }
            }
            else
            {
                paData.playbackEndPos = m.endPos;
                if (currentPos >= requestedEnd)
                {
                    activeState.playbackPosition =
                        static_cast<int64_t>(m.startPos);
                }
            }

            if (activeState.playbackPosition >=
                static_cast<int64_t>(paData.playbackEndPos))
            {
                paData.playbackBuffer.reset();
                paData.playbackChannelCount = 0;
                activeState.isPlaying = false;
                activeState.playbackPosition = -1;
            }
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

bool AudioDevices::hasPendingRecordedAudio() const noexcept
{
    return recordedChunkQueue.size_approx() > 0;
}

bool AudioDevices::takeRecordingOverflow() noexcept
{
    return recordingOverflowed.exchange(false, std::memory_order_acq_rel);
}

void AudioDevices::clearRecordedChunks()
{
    RecordedChunk chunk{};
    while (recordedChunkQueue.try_dequeue(chunk))
    {
    }
    recordingOverflowed.store(false, std::memory_order_release);
}

bool AudioDevices::prepareInputMonitorForTesting(
    const uint8_t inputChannels,
    std::unique_ptr<MonitorCancellationBackend> backend)
{
    auto candidate = std::make_unique<InputMonitorPipeline>();
    const MonitorStreamFormat format{.sampleRate = SAMPLE_RATE,
                                     .callbackFrames = BUFFER_SIZE,
                                     .inputChannels = inputChannels,
                                     .outputChannels = 2};
    if (!candidate->prepare(format, std::move(backend)))
    {
        return false;
    }
    candidate->setFeedbackSuppressionMode(
        feedbackSuppressionMode.load(std::memory_order_acquire));
    paData.inputChannelCount = inputChannels;
    monitorPipeline = std::move(candidate);
    return true;
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

    const bool needsInput = isInputMonitoringEnabled() || isRecording();
    const bool opened = openDevice(needsInput ? selection.inputDeviceIndex : -1,
                                   selection.outputDeviceIndex);
    if (isInputMonitoringEnabled() &&
        (!opened || !monitorPipeline || !monitorPipeline->isPrepared() ||
         !monitorPipeline->isModeAvailable(
             feedbackSuppressionMode.load(std::memory_order_acquire))))
    {
        inputMonitoringError.store(
            opened ? InputMonitoringError::SuppressionUnavailable
                   : InputMonitoringError::DeviceUnavailable,
            std::memory_order_release);
        inputMonitoringRequested.store(false, std::memory_order_release);
        Base::enqueue(SetInputMonitoring{.enabled = false});
    }
    return true;
}
