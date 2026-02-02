#pragma once

#include "concurrency/AtomicStateExchange.hpp"
#include "AudioMessage.hpp"
#include "AudioDeviceView.hpp"
#include "AudioDeviceState.hpp"

typedef void PaStream;
struct PaStreamCallbackTimeInfo;
typedef unsigned long PaStreamCallbackFlags;

namespace cupuacu
{
    class AudioDevice
        : public concurrency::AtomicStateExchange<AudioDeviceState,
                                                  AudioDeviceView, AudioMessage>
    {
    public:
        AudioDevice();
        ~AudioDevice();

        void openDevice();
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

        PaStream *stream = nullptr;
    };
} // namespace cupuacu
