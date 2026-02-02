#include "AudioDeviceView.hpp"

#include "AudioDeviceState.hpp"

using namespace cupuacu;

AudioDeviceView::AudioDeviceView(const AudioDeviceState *s) noexcept : state(s)
{
}

bool AudioDeviceView::isPlaying() const
{
    return state->isPlaying;
}

int64_t AudioDeviceView::getPlaybackPosition() const
{
    return state->playbackPosition;
}
