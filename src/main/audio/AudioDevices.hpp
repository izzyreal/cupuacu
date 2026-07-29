#pragma once

#include "concurrency/AtomicStateExchange.hpp"
#include "audio/AudioBuffer.hpp"
#include "audio/AudioDeviceState.hpp"
#include "audio/AudioDeviceView.hpp"
#include "audio/AudioMessage.hpp"
#include "audio/AudioProcessor.hpp"
#include "audio/AudioCallbackCore.hpp"
#include "audio/InputMonitorPipeline.hpp"
#include "audio/RecordedChunk.hpp"

#include <atomic>
#include <cstdint>
#include <limits>
#include <mutex>

#include <readerwriterqueue.h>

typedef void PaStream;
struct PaStreamCallbackTimeInfo;
typedef unsigned long PaStreamCallbackFlags;

namespace cupuacu::gui
{
    class VuMeter;
}

namespace cupuacu
{
    class Document;
}

namespace cupuacu::audio
{
    class AudioDevices
        : public concurrency::AtomicStateExchange<AudioDeviceState,
                                                  AudioDeviceView, AudioMessage>
    {
        using Base =
            concurrency::AtomicStateExchange<AudioDeviceState, AudioDeviceView,
                                             AudioMessage>;

    public:
        static constexpr std::size_t kMaxRecordedChannels =
            cupuacu::audio::kMaxRecordedChannels;
        static constexpr std::size_t kRecordedChunkFrames =
            cupuacu::audio::kRecordedChunkFrames;
        using RecordedChunk = cupuacu::audio::RecordedChunk;

        struct DeviceSelection
        {
            int hostApiIndex = -1;
            int outputDeviceIndex = -1;
            int inputDeviceIndex = -1;

            bool operator==(const DeviceSelection &other) const noexcept
            {
                return hostApiIndex == other.hostApiIndex &&
                       outputDeviceIndex == other.outputDeviceIndex &&
                       inputDeviceIndex == other.inputDeviceIndex;
            }
            bool operator!=(const DeviceSelection &other) const noexcept
            {
                return !(*this == other);
            }
        };

        explicit AudioDevices(bool openDefaultDevice = true);
        ~AudioDevices();

        using Base::enqueue;
        void enqueue(Play msg) noexcept;
        void enqueue(Record msg) noexcept;

        bool openDevice(int inputDeviceIndex, int outputDeviceIndex);
        void closeDevice();
        void prepareForRecording();
        bool setInputMonitoringEnabled(bool enabled,
                                       gui::VuMeter *vuMeter = nullptr);
        bool isInputMonitoringEnabled() const noexcept;
        InputMonitoringError getInputMonitoringError() const noexcept;
        MonitorProtectionTelemetry getMonitorProtectionTelemetry() const;
        FeedbackSuppressionMode getFeedbackSuppressionMode() const noexcept;
        bool setFeedbackSuppressionMode(FeedbackSuppressionMode mode) noexcept;
        void releaseInputIfUnused();

        bool isPlaying() const;
        bool isRecording() const;
        int64_t getPlaybackPosition() const;
        int64_t getRecordingPosition() const;
        bool popRecordedChunk(RecordedChunk &outChunk);
        [[nodiscard]] bool hasPendingRecordedAudio() const noexcept;
        [[nodiscard]] bool takeRecordingOverflow() noexcept;
        void clearRecordedChunks();
        bool prepareInputMonitorForTesting(
            uint8_t inputChannels,
            std::unique_ptr<MonitorCancellationBackend> backend = nullptr);
        int
        processCallbackCycle(const float *inputBuffer, void *outputBuffer,
                             unsigned long framesPerBuffer,
                             const AudioCallbackTiming &timing = {}) noexcept;

        DeviceSelection getDeviceSelection() const;
        bool setDeviceSelection(const DeviceSelection &selection);

    protected:
        void applyMessage(const AudioMessage &msg) noexcept override;

    private:
        struct PaData
        {
            std::shared_ptr<cupuacu::audio::AudioBuffer> playbackBuffer;
            bool selectionIsActive = false;
            cupuacu::SelectedChannels selectedChannels =
                cupuacu::SelectedChannels::BOTH;
            AudioDevices *device = nullptr;
            uint8_t playbackChannelCount = 0;
            uint64_t playbackStartPos = 0;
            uint64_t playbackEndPos = 0;
            bool playbackLoopEnabled = false;
            bool playbackHasPendingSwitch = false;
            uint64_t playbackPendingStartPos = 0;
            uint64_t playbackPendingEndPos = 0;
            std::shared_ptr<const AudioProcessor> previewProcessor;
            uint64_t recordingEndPos = std::numeric_limits<uint64_t>::max();
            bool recordingBoundedToEnd = false;
            uint8_t recordingDocumentChannelCount = 0;
            uint8_t inputChannelCount = 0;
            gui::VuMeter *vuMeter = nullptr;
            bool monitorWasSuspendedForPlayback = false;
        };

        static int paCallback(const void *inputBuffer, void *outputBuffer,
                              unsigned long framesPerBuffer,
                              const PaStreamCallbackTimeInfo *timeInfo,
                              PaStreamCallbackFlags statusFlags,
                              void *userData);

        static void writeSilenceToOutput(float *out, unsigned long frames);
        static bool
        fillOutputBuffer(PaData &data, float *out,
                         unsigned long framesPerBuffer,
                         callback_core::StereoMeterLevels &meterLevels);
        static void
        recordInputIntoQueue(PaData &data, const float *input,
                             unsigned long framesPerBuffer,
                             callback_core::StereoMeterLevels &meterLevels);
        static void
        pushPeaksToVuMeter(PaData &data,
                           const callback_core::StereoMeterLevels &meterLevels,
                           bool isPlaying, bool isRecording, bool isMonitoring);
        static void snapshotQueuedPlayMessage(Play &msg);
        static void snapshotQueuedRecordMessage(Record &msg);

        void closeDeviceLocked();

        std::mutex streamMutex;
        mutable std::mutex selectionMutex;
        std::atomic_bool inputMonitoringRequested{false};
        std::atomic<InputMonitoringError> inputMonitoringError{
            InputMonitoringError::None};
        std::atomic<FeedbackSuppressionMode> feedbackSuppressionMode{
            FeedbackSuppressionMode::Standard};
        int currentInputDeviceIndex = -1;
        int currentOutputDeviceIndex = -1;
        PaStream *stream = nullptr;
        DeviceSelection deviceSelection;
        moodycamel::ReaderWriterQueue<RecordedChunk> recordedChunkQueue{512};
        std::atomic_bool recordingOverflowed{false};
        std::unique_ptr<InputMonitorPipeline> monitorPipeline;
        PaData paData;
        uint64_t monitorTripGeneration = 0;
    };
} // namespace cupuacu::audio
