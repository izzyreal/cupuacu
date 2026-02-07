#include "audio/AudioDeviceView.hpp"

#include "audio/AudioDeviceState.hpp"

using namespace cupuacu;
using namespace cupuacu::audio;
AudioDeviceView::AudioDeviceView(const AudioDeviceState *s) noexcept : state(s)
{
}

bool AudioDeviceView::isPlaying() const
{
    return state->isPlaying;
}

bool AudioDeviceView::isRecording() const
{
    return state->isRecording;
}

int64_t AudioDeviceView::getPlaybackPosition() const
{
    return state->playbackPosition;
}

int64_t AudioDeviceView::getRecordingPosition() const
{
    return state->recordingPosition;
}
