#pragma once

#include "concurrency/AtomicStateExchange.hpp"
#include "audio/AudioMessage.hpp"
#include "audio/AudioDeviceView.hpp"
#include "audio/AudioDeviceState.hpp"

#include <mutex>

typedef void PaStream;
struct PaStreamCallbackTimeInfo;
typedef unsigned long PaStreamCallbackFlags;

namespace cupuacu::audio
{
    class AudioDevice
        : public concurrency::AtomicStateExchange<AudioDeviceState,
                                                  AudioDeviceView, AudioMessage>
    {
    public:
        AudioDevice();
        ~AudioDevice();

        void openDevice(int inputDeviceIndex, int outputDeviceIndex);
        void closeDevice();

        bool isPlaying() const;
        int64_t getPlaybackPosition() const;

    protected:
        void applyMessage(const AudioMessage &msg) noexcept override;

    private:
        static int paCallback(const void *inputBuffer, void *outputBuffer,
                              unsigned long framesPerBuffer,
                              const PaStreamCallbackTimeInfo *timeInfo,
                              PaStreamCallbackFlags statusFlags,
                              void *userData);

        void closeDeviceLocked();

        std::mutex streamMutex;
        int currentInputDeviceIndex = -1;
        int currentOutputDeviceIndex = -1;
        PaStream *stream = nullptr;
    };
} // namespace cupuacu::audio
