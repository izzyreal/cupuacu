#pragma once

#include "concurrency/AtomicStateExchange.hpp"
#include "audio/AudioDeviceState.hpp"
#include "audio/AudioDeviceView.hpp"
#include "audio/AudioMessage.hpp"

#include <array>
#include <cstdint>
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
    public:
        static constexpr std::size_t kMaxRecordedChannels = 2;
        static constexpr std::size_t kRecordedChunkFrames = 256;

        struct RecordedChunk
        {
            int64_t startFrame = 0;
            uint32_t frameCount = 0;
            uint8_t channelCount = 0;
            std::array<float, kRecordedChunkFrames * kMaxRecordedChannels>
                interleavedSamples{};
        };

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

        AudioDevices();
        ~AudioDevices();

        void openDevice(int inputDeviceIndex, int outputDeviceIndex);
        void closeDevice();

        bool isPlaying() const;
        bool isRecording() const;
        int64_t getPlaybackPosition() const;
        int64_t getRecordingPosition() const;
        bool popRecordedChunk(RecordedChunk &outChunk);
        void clearRecordedChunks();

        DeviceSelection getDeviceSelection() const;
        bool setDeviceSelection(const DeviceSelection &selection);

    protected:
        void applyMessage(const AudioMessage &msg) noexcept override;

    private:
        struct PaData
        {
            cupuacu::Document *playbackDocument = nullptr;
            cupuacu::Document *recordingDocument = nullptr;
            bool selectionIsActive = false;
            cupuacu::SelectedChannels selectedChannels;
            AudioDevices *device = nullptr;
            uint64_t playbackEndPos = 0;
            gui::VuMeter *vuMeter = nullptr;
        };

        static int paCallback(const void *inputBuffer, void *outputBuffer,
                              unsigned long framesPerBuffer,
                              const PaStreamCallbackTimeInfo *timeInfo,
                              PaStreamCallbackFlags statusFlags,
                              void *userData);

        static void writeSilenceToOutput(float *out, unsigned long frames);
        static bool fillOutputBuffer(PaData &data, float *out,
                                     unsigned long framesPerBuffer,
                                     float &peakLeft, float &peakRight);
        static void recordInputIntoQueue(PaData &data, const float *input,
                                         unsigned long framesPerBuffer,
                                         float &peakLeft, float &peakRight);
        static void pushPeaksToVuMeter(PaData &data, float peakLeft,
                                       float peakRight, bool isPlaying,
                                       bool isRecording);

        void closeDeviceLocked();

        std::mutex streamMutex;
        mutable std::mutex selectionMutex;
        int currentInputDeviceIndex = -1;
        int currentOutputDeviceIndex = -1;
        PaStream *stream = nullptr;
        DeviceSelection deviceSelection;
        moodycamel::ReaderWriterQueue<RecordedChunk> recordedChunkQueue{512};
        PaData paData;
    };
} // namespace cupuacu::audio
