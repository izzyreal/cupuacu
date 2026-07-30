#include "audio/AudioDevices.hpp"
#include "audio/AudioCallbackCore.hpp"

#include "Document.hpp"
#include "PaUtil.hpp"
#include "gui/VuMeter.hpp"
#include "utils/VariantUtils.hpp"

#include <portaudio.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <sstream>

#if CUPUACU_RTSAN_LIBS_ENABLED
#include <rtsan_standalone/rtsan_standalone.h>
#endif

using namespace cupuacu;
using namespace cupuacu::audio;
using namespace cupuacu::utils;

namespace
{
    constexpr unsigned long BUFFER_SIZE = 256;
    constexpr std::array<double, 11> kDisplayedSampleRates{
        8000.0,  11025.0, 16000.0, 22050.0,  32000.0, 44100.0,
        48000.0, 88200.0, 96000.0, 176400.0, 192000.0};

    bool enqueueRecordedChunk(void *userdata,
                              const cupuacu::audio::RecordedChunk &chunk)
    {
        auto *queue = static_cast<
            moodycamel::ReaderWriterQueue<cupuacu::audio::RecordedChunk> *>(
            userdata);
        return queue->try_enqueue(chunk);
    }

    const char *purposeName(const AudioStreamPurpose purpose)
    {
        switch (purpose)
        {
            case AudioStreamPurpose::Playback:
                return "playback";
            case AudioStreamPurpose::Recording:
                return "recording";
            case AudioStreamPurpose::Monitoring:
                return "input monitoring";
            case AudioStreamPurpose::Probe:
                return "audio configuration";
        }
        return "audio";
    }

    const char *stageName(const AudioStreamFailureStage stage)
    {
        switch (stage)
        {
            case AudioStreamFailureStage::Validation:
                return "validation";
            case AudioStreamFailureStage::FormatProbe:
                return "format check";
            case AudioStreamFailureStage::Open:
                return "stream open";
            case AudioStreamFailureStage::Start:
                return "stream start";
            case AudioStreamFailureStage::MonitorPreparation:
                return "monitor preparation";
        }
        return "setup";
    }

    AudioStreamSetupResult failureResult(const AudioStreamRequest &request,
                                         const AudioStreamFailureStage stage,
                                         const PaError error,
                                         std::string message = {})
    {
        AudioStreamFailure failure{.stage = stage,
                                   .portAudioError = error,
                                   .message = std::move(message),
                                   .request = request};
        if (failure.message.empty() && error != paNoError)
        {
            if (const char *text = Pa_GetErrorText(error))
            {
                failure.message = text;
            }
        }
        if (error == paUnanticipatedHostError)
        {
            if (const PaHostErrorInfo *host = Pa_GetLastHostErrorInfo();
                host && host->errorText)
            {
                failure.hostMessage = host->errorText;
            }
        }
        return {.failure = std::move(failure)};
    }

    AudioStreamSetupResult makeParameters(const AudioStreamRequest &request,
                                          PaStreamParameters &input,
                                          const PaStreamParameters *&inputPtr,
                                          PaStreamParameters &output,
                                          const PaStreamParameters *&outputPtr)
    {
        inputPtr = nullptr;
        outputPtr = nullptr;
        if (request.sampleRate <= 0.0)
        {
            return failureResult(request, AudioStreamFailureStage::Validation,
                                 paInvalidSampleRate,
                                 "The document has no valid sample rate.");
        }
        if (request.purpose == AudioStreamPurpose::Playback &&
            !request.hasOutput())
        {
            return failureResult(request, AudioStreamFailureStage::Validation,
                                 paInvalidDevice,
                                 "No output device is selected.");
        }
        if (request.purpose == AudioStreamPurpose::Recording &&
            !request.hasInput())
        {
            return failureResult(request, AudioStreamFailureStage::Validation,
                                 paInvalidDevice,
                                 "No input device is selected.");
        }
        if (request.purpose == AudioStreamPurpose::Monitoring &&
            !request.isDuplex())
        {
            return failureResult(request, AudioStreamFailureStage::Validation,
                                 paBadIODeviceCombination,
                                 "Input monitoring requires both an input and "
                                 "an output device.");
        }
        if (!request.hasInput() && !request.hasOutput())
        {
            return failureResult(request, AudioStreamFailureStage::Validation,
                                 paBadIODeviceCombination,
                                 "No input or output device is selected.");
        }
        if (request.hasInput())
        {
            const PaDeviceInfo *info =
                Pa_GetDeviceInfo(request.inputDeviceIndex);
            if (!info)
            {
                return failureResult(
                    request, AudioStreamFailureStage::Validation,
                    paInvalidDevice,
                    "The selected input device is unavailable.");
            }
            if (request.inputChannels > info->maxInputChannels)
            {
                return failureResult(request,
                                     AudioStreamFailureStage::Validation,
                                     paInvalidChannelCount,
                                     "The input device does not provide the "
                                     "required channel count.");
            }
            input.device = request.inputDeviceIndex;
            input.channelCount = request.inputChannels;
            input.sampleFormat = paFloat32;
            input.suggestedLatency = info->defaultLowInputLatency;
            input.hostApiSpecificStreamInfo = nullptr;
            inputPtr = &input;
        }
        if (request.hasOutput())
        {
            const PaDeviceInfo *info =
                Pa_GetDeviceInfo(request.outputDeviceIndex);
            if (!info)
            {
                return failureResult(
                    request, AudioStreamFailureStage::Validation,
                    paInvalidDevice,
                    "The selected output device is unavailable.");
            }
            if (request.outputChannels > info->maxOutputChannels)
            {
                return failureResult(request,
                                     AudioStreamFailureStage::Validation,
                                     paInvalidChannelCount,
                                     "The output device does not provide the "
                                     "required channel count.");
            }
            output.device = request.outputDeviceIndex;
            output.channelCount = request.outputChannels;
            output.sampleFormat = paFloat32;
            output.suggestedLatency = info->defaultLowOutputLatency;
            output.hostApiSpecificStreamInfo = nullptr;
            outputPtr = &output;
        }
        return {};
    }
} // namespace

AudioDevices::AudioDevices(const bool usePortAudioStreamsToUse)
    : concurrency::AtomicStateExchange<AudioDeviceState, AudioDeviceView,
                                       AudioMessage>([](AudioDeviceState &) {}),
      usePortAudioStreams(usePortAudioStreamsToUse)
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

    int recordingChannels = data.recordingDocumentChannelCount;
    if (recordingChannels <= 0)
    {
        recordingChannels = static_cast<int>(
            std::max<uint8_t>(data.inputChannelCount, uint8_t{1}));
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

    const uint8_t inputChannels = data.inputChannelCount > 0
                                      ? data.inputChannelCount
                                      : static_cast<uint8_t>(recordingChannels);
    if (!callback_core::recordInputIntoChunks(
            input, framesToRecord, inputChannels,
            static_cast<uint8_t>(recordingChannels), state->recordingPosition,
            static_cast<void *>(&data.device->recordedChunkQueue),
            enqueueRecordedChunk, meterLevels))
    {
        data.device->recordingOverflowed.store(true, std::memory_order_release);
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
    auto *deviceOutput = static_cast<float *>(outputBuffer);
    float *stereoOutput = deviceOutput;
    const bool collapseToMono = deviceOutput && paData.outputChannelCount == 1;
    if (collapseToMono)
    {
        if (framesPerBuffer > BUFFER_SIZE)
        {
            std::fill_n(deviceOutput, framesPerBuffer, 0.0f);
            publishState();
            return 0;
        }
        stereoOutput = paData.stereoOutputScratch.data();
    }

    const bool playedAnyFrame =
        stereoOutput ? fillOutputBuffer(paData, stereoOutput, framesPerBuffer,
                                        meterLevels)
                     : false;
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
            inputBuffer, stereoOutput, framesPerBuffer, timing);
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

    if (collapseToMono)
    {
        for (unsigned long frame = 0; frame < framesPerBuffer; ++frame)
        {
            deviceOutput[frame] =
                0.5f * (stereoOutput[frame * 2] + stereoOutput[frame * 2 + 1]);
        }
    }

    publishState();
    return 0;
}

AudioStreamSetupResult
AudioDevices::probeStream(const AudioStreamRequest &request) const
{
    PaStreamParameters input{};
    PaStreamParameters output{};
    const PaStreamParameters *inputPtr = nullptr;
    const PaStreamParameters *outputPtr = nullptr;
    if (auto result =
            makeParameters(request, input, inputPtr, output, outputPtr);
        !result)
    {
        return result;
    }

    const PaError error =
        Pa_IsFormatSupported(inputPtr, outputPtr, request.sampleRate);
    if (error != paFormatIsSupported)
    {
        return failureResult(request, AudioStreamFailureStage::FormatProbe,
                             error);
    }
    return {};
}

AudioStreamSetupResult AudioDevices::chooseSupportedChannelCount(
    const int deviceIndex, const bool isInput, const double sampleRate,
    const uint8_t preferredChannels, const AudioStreamPurpose purpose,
    uint8_t &selectedChannels) const
{
    selectedChannels = 0;
    const PaDeviceInfo *info = Pa_GetDeviceInfo(deviceIndex);
    const int maximumChannels =
        info ? (isInput ? info->maxInputChannels : info->maxOutputChannels) : 0;
    const uint8_t preferred = std::clamp<uint8_t>(preferredChannels, 1, 2);
    const std::array<uint8_t, 2> candidates{
        preferred, static_cast<uint8_t>(preferred == 1 ? 2 : 1)};
    std::optional<AudioStreamSetupResult> lastFailure;

    for (const uint8_t channels : candidates)
    {
        if (channels > maximumChannels)
        {
            continue;
        }
        AudioStreamRequest request{.purpose =
                                       purpose == AudioStreamPurpose::Monitoring
                                           ? AudioStreamPurpose::Probe
                                           : purpose,
                                   .sampleRate = sampleRate};
        if (isInput)
        {
            request.inputDeviceIndex = deviceIndex;
            request.inputChannels = channels;
        }
        else
        {
            request.outputDeviceIndex = deviceIndex;
            request.outputChannels = channels;
        }
        auto result = probeStream(request);
        if (result)
        {
            selectedChannels = channels;
            return {};
        }
        if (result.failure)
        {
            result.failure->request.purpose = purpose;
        }
        lastFailure = std::move(result);
    }

    if (lastFailure)
    {
        return std::move(*lastFailure);
    }

    AudioStreamRequest request{.purpose = purpose, .sampleRate = sampleRate};
    if (isInput)
    {
        request.inputDeviceIndex = deviceIndex;
        request.inputChannels = preferred;
    }
    else
    {
        request.outputDeviceIndex = deviceIndex;
        request.outputChannels = preferred;
    }
    return failureResult(
        request, AudioStreamFailureStage::Validation, paInvalidChannelCount,
        isInput ? "The input device does not provide an audio channel."
                : "The output device does not provide an audio channel.");
}

AudioStreamSetupResult
AudioDevices::openStream(const AudioStreamRequest &request)
{
    std::lock_guard<std::mutex> lock(streamMutex);
    return openStreamLocked(request, true);
}

AudioStreamSetupResult
AudioDevices::openStreamLocked(const AudioStreamRequest &request,
                               const bool startStream)
{
    const bool unchanged =
        stream && request.inputDeviceIndex == currentInputDeviceIndex &&
        request.outputDeviceIndex == currentOutputDeviceIndex &&
        request.sampleRate == currentSampleRate &&
        request.inputChannels == currentInputChannelCount &&
        request.outputChannels == currentOutputChannelCount;
    if (unchanged)
    {
        return {};
    }

    PaStreamParameters input{};
    PaStreamParameters output{};
    const PaStreamParameters *inputPtr = nullptr;
    const PaStreamParameters *outputPtr = nullptr;
    if (auto result =
            makeParameters(request, input, inputPtr, output, outputPtr);
        !result)
    {
        return result;
    }

    const PaError support =
        Pa_IsFormatSupported(inputPtr, outputPtr, request.sampleRate);
    if (support != paFormatIsSupported)
    {
        return failureResult(request, AudioStreamFailureStage::FormatProbe,
                             support);
    }

    closeDeviceLocked();
    PaStream *candidateStream = nullptr;
    paData.device = this;
    PaError error =
        Pa_OpenStream(&candidateStream, inputPtr, outputPtr, request.sampleRate,
                      BUFFER_SIZE, paNoFlag, paCallback, &paData);
    if (error != paNoError)
    {
        return failureResult(request, AudioStreamFailureStage::Open, error);
    }

    stream = candidateStream;
    currentInputDeviceIndex =
        request.hasInput() ? request.inputDeviceIndex : -1;
    currentOutputDeviceIndex =
        request.hasOutput() ? request.outputDeviceIndex : -1;
    currentSampleRate = request.sampleRate;
    currentInputChannelCount = request.inputChannels;
    currentOutputChannelCount = request.outputChannels;
    paData.inputChannelCount = request.inputChannels;
    paData.outputChannelCount = request.outputChannels;

    monitorPipeline.reset();
    if (request.purpose == AudioStreamPurpose::Monitoring ||
        (request.isDuplex() && isInputMonitoringEnabled()))
    {
        const PaStreamInfo *streamInfo = Pa_GetStreamInfo(stream);
        MonitorStreamFormat monitorFormat{
            .sampleRate =
                streamInfo
                    ? static_cast<int>(std::lround(streamInfo->sampleRate))
                    : static_cast<int>(std::lround(request.sampleRate)),
            .callbackFrames = BUFFER_SIZE,
            .inputChannels = request.inputChannels,
            .outputChannels = request.outputChannels,
            .inputLatencySeconds =
                streamInfo ? streamInfo->inputLatency : input.suggestedLatency,
            .outputLatencySeconds = streamInfo ? streamInfo->outputLatency
                                               : output.suggestedLatency};
        auto candidate = std::make_unique<InputMonitorPipeline>();
        if (candidate->prepare(monitorFormat))
        {
            candidate->setFeedbackSuppressionMode(
                feedbackSuppressionMode.load(std::memory_order_acquire));
            monitorPipeline = std::move(candidate);
        }
        else
        {
            closeDeviceLocked();
            return failureResult(
                request, AudioStreamFailureStage::MonitorPreparation,
                paInternalError,
                "The input monitoring pipeline could not be prepared.");
        }
    }

    if (!startStream)
    {
        return {};
    }

    error = Pa_StartStream(stream);
    if (error != paNoError)
    {
        auto failure =
            failureResult(request, AudioStreamFailureStage::Start, error);
        closeDeviceLocked();
        return failure;
    }
    if (monitorPipeline &&
        inputMonitoringRequested.load(std::memory_order_acquire))
    {
        monitorPipeline->startSession();
    }
    return {};
}

void AudioDevices::closeDevice()
{
    std::lock_guard<std::mutex> lock(streamMutex);
    closeDeviceLocked();
    currentInputDeviceIndex = -1;
    currentOutputDeviceIndex = -1;
    currentSampleRate = 0.0;
    currentInputChannelCount = 0;
    currentOutputChannelCount = 0;
    paData.inputChannelCount = 0;
    paData.outputChannelCount = 0;
}

AudioStreamSetupResult
AudioDevices::prepareForPlayback(const cupuacu::Document &document)
{
    if (!usePortAudioStreams)
    {
        return {};
    }
    const auto selection = getDeviceSelection();
    const auto documentChannels = static_cast<uint8_t>(
        std::clamp<int64_t>(document.getChannelCount(), 0, 2));
    const double sampleRate = static_cast<double>(document.getSampleRate());
    const bool monitoring = isInputMonitoringEnabled();
    AudioStreamRequest request{.purpose = monitoring
                                              ? AudioStreamPurpose::Monitoring
                                              : AudioStreamPurpose::Playback,
                               .outputDeviceIndex = selection.outputDeviceIndex,
                               .sampleRate = sampleRate,
                               .outputChannels = documentChannels};
    if (selection.outputDeviceIndex >= 0)
    {
        if (auto result = chooseSupportedChannelCount(
                selection.outputDeviceIndex, false, sampleRate,
                documentChannels, request.purpose, request.outputChannels);
            !result)
        {
            return result;
        }
    }
    if (monitoring)
    {
        request.inputDeviceIndex = selection.inputDeviceIndex;
        request.inputChannels = documentChannels;
        if (selection.inputDeviceIndex >= 0)
        {
            if (auto result = chooseSupportedChannelCount(
                    selection.inputDeviceIndex, true, sampleRate,
                    documentChannels, request.purpose, request.inputChannels);
                !result)
            {
                return result;
            }
        }
    }
    return openStream(request);
}

AudioStreamSetupResult
AudioDevices::prepareForRecording(const cupuacu::Document &document)
{
    if (recordingPreparationResultForTesting &&
        !*recordingPreparationResultForTesting)
    {
        const AudioStreamRequest request{
            .purpose = AudioStreamPurpose::Recording,
            .sampleRate = static_cast<double>(document.getSampleRate())};
        return failureResult(request, AudioStreamFailureStage::Open,
                             paDeviceUnavailable,
                             "The selected input device is unavailable.");
    }
    if (recordingPreparationResultForTesting)
    {
        return {};
    }
    const auto selection = getDeviceSelection();
    const auto documentChannels = static_cast<uint8_t>(
        std::clamp<int64_t>(document.getChannelCount(), 0, 2));
    const double sampleRate = static_cast<double>(document.getSampleRate());
    const bool monitoring = isInputMonitoringEnabled();
    AudioStreamRequest request{.purpose = monitoring
                                              ? AudioStreamPurpose::Monitoring
                                              : AudioStreamPurpose::Recording,
                               .inputDeviceIndex = selection.inputDeviceIndex,
                               .sampleRate = sampleRate,
                               .inputChannels = documentChannels};
    if (selection.inputDeviceIndex >= 0)
    {
        if (auto result = chooseSupportedChannelCount(
                selection.inputDeviceIndex, true, sampleRate, documentChannels,
                request.purpose, request.inputChannels);
            !result)
        {
            return result;
        }
    }
    if (monitoring)
    {
        request.outputDeviceIndex = selection.outputDeviceIndex;
        request.outputChannels = documentChannels;
        if (selection.outputDeviceIndex >= 0)
        {
            if (auto result = chooseSupportedChannelCount(
                    selection.outputDeviceIndex, false, sampleRate,
                    documentChannels, request.purpose, request.outputChannels);
                !result)
            {
                return result;
            }
        }
    }
    return openStream(request);
}

void AudioDevices::setRecordingPreparationResultForTesting(const bool result)
{
    recordingPreparationResultForTesting = result;
}

AudioStreamSetupResult
AudioDevices::setInputMonitoringEnabled(const bool enabled,
                                        const cupuacu::Document *document,
                                        gui::VuMeter *vuMeter)
{
    if (enabled)
    {
        if (isPlaying())
        {
            return failureResult(
                {.purpose = AudioStreamPurpose::Monitoring},
                AudioStreamFailureStage::Validation, paStreamIsNotStopped,
                "Stop playback before enabling input monitoring.");
        }
        if (!document || document->getSampleRate() <= 0 ||
            document->getChannelCount() <= 0)
        {
            return failureResult({.purpose = AudioStreamPurpose::Monitoring},
                                 AudioStreamFailureStage::Validation,
                                 paInvalidSampleRate,
                                 "Create or open a formatted document before "
                                 "enabling input monitoring.");
        }

        if (!isRecording())
        {
            const auto selection = getDeviceSelection();
            const auto documentChannels = static_cast<uint8_t>(
                std::clamp<int64_t>(document->getChannelCount(), 0, 2));
            const double sampleRate =
                static_cast<double>(document->getSampleRate());
            AudioStreamRequest request{
                .purpose = AudioStreamPurpose::Monitoring,
                .inputDeviceIndex = selection.inputDeviceIndex,
                .outputDeviceIndex = selection.outputDeviceIndex,
                .sampleRate = sampleRate,
                .inputChannels = documentChannels,
                .outputChannels = documentChannels};
            if (selection.inputDeviceIndex >= 0)
            {
                if (auto result = chooseSupportedChannelCount(
                        selection.inputDeviceIndex, true, sampleRate,
                        documentChannels, request.purpose,
                        request.inputChannels);
                    !result)
                {
                    return result;
                }
            }
            if (selection.outputDeviceIndex >= 0)
            {
                if (auto result = chooseSupportedChannelCount(
                        selection.outputDeviceIndex, false, sampleRate,
                        documentChannels, request.purpose,
                        request.outputChannels);
                    !result)
                {
                    return result;
                }
            }
            if (auto result = openStream(request); !result)
            {
                inputMonitoringError.store(
                    InputMonitoringError::DeviceUnavailable,
                    std::memory_order_release);
                return result;
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
            return failureResult(
                {.purpose = AudioStreamPurpose::Monitoring},
                AudioStreamFailureStage::MonitorPreparation, paInternalError,
                "Feedback suppression could not be initialized.");
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
        closeDevice();
    }
    return {};
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
    closeDevice();
}

void AudioDevices::closeDeviceLocked()
{
    if (!stream)
    {
        currentInputDeviceIndex = -1;
        currentOutputDeviceIndex = -1;
        currentSampleRate = 0.0;
        currentInputChannelCount = 0;
        currentOutputChannelCount = 0;
        paData.inputChannelCount = 0;
        paData.outputChannelCount = 0;
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
    currentInputDeviceIndex = -1;
    currentOutputDeviceIndex = -1;
    currentSampleRate = 0.0;
    currentInputChannelCount = 0;
    currentOutputChannelCount = 0;
    paData.inputChannelCount = 0;
    paData.outputChannelCount = 0;
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

bool AudioDevices::hasOpenStream() const
{
    std::lock_guard<std::mutex> lock(streamMutex);
    return stream != nullptr;
}

bool AudioDevices::currentDuplexStreamMatches(const double sampleRate) const
{
    std::lock_guard<std::mutex> lock(streamMutex);
    return stream && currentSampleRate == sampleRate &&
           currentInputChannelCount > 0 && currentOutputChannelCount > 0;
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
    const MonitorStreamFormat format{.sampleRate = 44100,
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

    return true;
}

AudioStreamSetupResult
AudioDevices::trySetDeviceSelection(const DeviceSelection &selection,
                                    const double sampleRate,
                                    const uint8_t channelCount)
{
    if (selection == getDeviceSelection())
    {
        return {};
    }
    if (isPlaying() || isRecording())
    {
        return failureResult(
            {.purpose = AudioStreamPurpose::Probe},
            AudioStreamFailureStage::Validation, paStreamIsNotStopped,
            "Stop playback or recording before changing audio devices.");
    }

    const auto rateFor = [sampleRate](const int deviceIndex)
    {
        if (sampleRate > 0.0)
        {
            return sampleRate;
        }
        if (const PaDeviceInfo *info = Pa_GetDeviceInfo(deviceIndex))
        {
            return info->defaultSampleRate;
        }
        return 0.0;
    };
    const uint8_t preferredChannels = std::clamp<uint8_t>(channelCount, 1, 2);
    uint8_t outputChannels = 0;
    uint8_t inputChannels = 0;

    if (selection.outputDeviceIndex >= 0)
    {
        if (auto result = chooseSupportedChannelCount(
                selection.outputDeviceIndex, false,
                rateFor(selection.outputDeviceIndex), preferredChannels,
                AudioStreamPurpose::Probe, outputChannels);
            !result)
        {
            return result;
        }
    }
    if (selection.inputDeviceIndex >= 0)
    {
        if (auto result = chooseSupportedChannelCount(
                selection.inputDeviceIndex, true,
                rateFor(selection.inputDeviceIndex), preferredChannels,
                AudioStreamPurpose::Probe, inputChannels);
            !result)
        {
            return result;
        }
    }

    if (isInputMonitoringEnabled())
    {
        const AudioStreamRequest previousRequest{
            .purpose = AudioStreamPurpose::Monitoring,
            .inputDeviceIndex = currentInputDeviceIndex,
            .outputDeviceIndex = currentOutputDeviceIndex,
            .sampleRate = currentSampleRate,
            .inputChannels = currentInputChannelCount,
            .outputChannels = currentOutputChannelCount};
        const AudioStreamRequest duplexRequest{
            .purpose = AudioStreamPurpose::Monitoring,
            .inputDeviceIndex = selection.inputDeviceIndex,
            .outputDeviceIndex = selection.outputDeviceIndex,
            .sampleRate = sampleRate,
            .inputChannels = inputChannels,
            .outputChannels = outputChannels};
        if (auto result = openStream(duplexRequest); !result)
        {
            if (previousRequest.isDuplex() && previousRequest.sampleRate > 0.0)
            {
                (void)openStream(previousRequest);
            }
            return result;
        }
    }

    setDeviceSelection(selection);
    return {};
}

DeviceRateSupport
AudioDevices::getSupportedSampleRates(const int deviceIndex, const bool isInput,
                                      const double activeRate) const
{
    DeviceRateSupport result;
    if (deviceIndex < 0)
    {
        return result;
    }

    std::vector<double> rates(kDisplayedSampleRates.begin(),
                              kDisplayedSampleRates.end());
    const auto appendRate = [&rates](const double rate)
    {
        if (rate <= 0.0 ||
            std::find(rates.begin(), rates.end(), rate) != rates.end())
        {
            return;
        }
        rates.push_back(rate);
    };
    appendRate(activeRate);
    if (const PaDeviceInfo *info = Pa_GetDeviceInfo(deviceIndex))
    {
        appendRate(info->defaultSampleRate);
    }
    std::sort(rates.begin(), rates.end());

    for (const double rate : rates)
    {
        for (uint8_t channels = 1; channels <= 2; ++channels)
        {
            AudioStreamRequest request{.purpose = AudioStreamPurpose::Probe,
                                       .sampleRate = rate};
            if (isInput)
            {
                request.inputDeviceIndex = deviceIndex;
                request.inputChannels = channels;
            }
            else
            {
                request.outputDeviceIndex = deviceIndex;
                request.outputChannels = channels;
            }
            if (probeStream(request))
            {
                (channels == 1 ? result.mono : result.stereo).push_back(rate);
            }
        }
    }
    return result;
}

std::string AudioDevices::describeFailure(const AudioStreamFailure &failure)
{
    std::ostringstream message;
    message << "Could not set up " << purposeName(failure.request.purpose)
            << " (" << stageName(failure.stage) << ").";
    if (failure.request.sampleRate > 0.0)
    {
        message << "\n\nRate: " << std::lround(failure.request.sampleRate)
                << " Hz";
    }
    if (failure.request.inputChannels > 0)
    {
        message << "\nInput channels: "
                << static_cast<int>(failure.request.inputChannels);
    }
    if (failure.request.outputChannels > 0)
    {
        message << "\nOutput channels: "
                << static_cast<int>(failure.request.outputChannels);
    }
    if (!failure.message.empty())
    {
        message << "\n\nPortAudio: " << failure.message;
    }
    if (!failure.hostMessage.empty() && failure.hostMessage != failure.message)
    {
        message << "\nHost: " << failure.hostMessage;
    }
    if (failure.request.purpose != AudioStreamPurpose::Probe)
    {
        message << "\n\nChoose input and output devices in Options > Audio.";
    }
    return message.str();
}
