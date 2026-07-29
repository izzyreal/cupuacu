#include "audio/AudioDeviceView.hpp"

#include "audio/AudioDeviceState.hpp"

using namespace cupuacu;
using namespace cupuacu::audio;
AudioDeviceView::AudioDeviceView(
    const AudioDeviceState *s,
    std::atomic<std::uint32_t> *readersToRelease) noexcept
    : state(s), readers(readersToRelease)
{
}

AudioDeviceView::~AudioDeviceView()
{
    release();
}

AudioDeviceView::AudioDeviceView(const AudioDeviceView &other) noexcept
    : state(other.state), readers(other.readers)
{
    if (readers)
    {
        readers->fetch_add(1, std::memory_order_acquire);
    }
}

AudioDeviceView &
AudioDeviceView::operator=(const AudioDeviceView &other) noexcept
{
    if (this == &other)
    {
        return *this;
    }
    release();
    state = other.state;
    readers = other.readers;
    if (readers)
    {
        readers->fetch_add(1, std::memory_order_acquire);
    }
    return *this;
}

AudioDeviceView::AudioDeviceView(AudioDeviceView &&other) noexcept
    : state(other.state), readers(other.readers)
{
    other.state = nullptr;
    other.readers = nullptr;
}

AudioDeviceView &
AudioDeviceView::operator=(AudioDeviceView &&other) noexcept
{
    if (this == &other)
    {
        return *this;
    }
    release();
    state = other.state;
    readers = other.readers;
    other.state = nullptr;
    other.readers = nullptr;
    return *this;
}

void AudioDeviceView::release() noexcept
{
    if (readers)
    {
        readers->fetch_sub(1, std::memory_order_release);
        readers = nullptr;
    }
    state = nullptr;
}

bool AudioDeviceView::isPlaying() const
{
    return state && state->isPlaying;
}

bool AudioDeviceView::isRecording() const
{
    return state && state->isRecording;
}

bool AudioDeviceView::isInputMonitoringEnabled() const
{
    return state && state->isInputMonitoringEnabled;
}

MonitorProtectionTelemetry
AudioDeviceView::getMonitorProtectionTelemetry() const
{
    return state ? state->monitorProtection : MonitorProtectionTelemetry{};
}

int64_t AudioDeviceView::getPlaybackPosition() const
{
    return state ? state->playbackPosition : -1;
}

int64_t AudioDeviceView::getRecordingPosition() const
{
    return state ? state->recordingPosition : -1;
}
