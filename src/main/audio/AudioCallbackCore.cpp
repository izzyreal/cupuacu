#include "AudioCallbackCore.hpp"
#include "MeterAccumulator.hpp"

#include <algorithm>

void cupuacu::audio::callback_core::writeSilenceToOutput(
    float *out, const unsigned long frames)
{
    if (!out)
    {
        return;
    }

    for (unsigned long i = 0; i < frames; ++i)
    {
        *out++ = 0.0f;
        *out++ = 0.0f;
    }
}

bool cupuacu::audio::callback_core::fillOutputBuffer(
    const std::shared_ptr<cupuacu::audio::AudioBuffer> &buffer,
    const uint8_t channelCount, const bool selectionIsActive,
    const cupuacu::SelectedChannels selectedChannels,
    int64_t &playbackPosition,
    uint64_t &playbackStartPos, uint64_t &playbackEndPos,
    const bool playbackLoopEnabled, bool &playbackHasPendingSwitch,
    uint64_t &playbackPendingStartPos, uint64_t &playbackPendingEndPos,
    bool &isPlaying, float *out, const unsigned long framesPerBuffer,
    StereoMeterLevels &meterLevels,
    const cupuacu::audio::AudioProcessor *processor,
    const uint64_t effectStartPos, const uint64_t effectEndPos,
    const cupuacu::SelectedChannels processorChannels)
{
    if (!out)
    {
        return false;
    }

    if (!buffer || (channelCount != 1 && channelCount != 2))
    {
        writeSilenceToOutput(out, framesPerBuffer);
        return false;
    }

    const auto chBufL = buffer->getImmutableChannelData(0);
    const auto chBufR = buffer->getImmutableChannelData(channelCount == 2 ? 1 : 0);

    const bool shouldPlayChannelL =
        !selectionIsActive || selectedChannels == cupuacu::SelectedChannels::BOTH ||
        selectedChannels == cupuacu::SelectedChannels::LEFT;

    const bool shouldPlayChannelR =
        !selectionIsActive || selectedChannels == cupuacu::SelectedChannels::BOTH ||
        selectedChannels == cupuacu::SelectedChannels::RIGHT;

    float *const outputStart = out;
    bool playedAnyFrame = false;
    bool capturedBufferStart = false;
    int64_t bufferStartFrame = 0;
    unsigned long playedFrameCount = 0;
    cupuacu::audio::StereoMeterAccumulator meterAccumulator;
    for (unsigned long i = 0; i < framesPerBuffer; ++i)
    {
        if (!isPlaying || playbackPosition < 0)
        {
            *out++ = 0.f;
            *out++ = 0.f;
            continue;
        }

        if (playbackPosition >= static_cast<int64_t>(playbackEndPos))
        {
            const bool canLoop =
                playbackLoopEnabled && playbackEndPos > playbackStartPos;
            if (canLoop)
            {
                if (playbackHasPendingSwitch)
                {
                    playbackStartPos = playbackPendingStartPos;
                    playbackEndPos = playbackPendingEndPos;
                    playbackHasPendingSwitch = false;
                }
                playbackPosition = static_cast<int64_t>(playbackStartPos);
            }
            else
            {
                isPlaying = false;
                playbackPosition = -1;
                *out++ = 0.f;
                *out++ = 0.f;
                continue;
            }
        }

        if (!capturedBufferStart)
        {
            bufferStartFrame = playbackPosition;
            capturedBufferStart = true;
        }

        const float outL = shouldPlayChannelL ? chBufL[playbackPosition] : 0.0f;
        const float outR = shouldPlayChannelR ? chBufR[playbackPosition] : 0.0f;

        *out++ = outL;
        *out++ = outR;

        meterAccumulator.addFrame(outL, outR);
        ++playbackPosition;
        ++playedFrameCount;
        playedAnyFrame = true;
    }

    if (playedAnyFrame && processor && effectEndPos > effectStartPos)
    {
        processor->process(outputStart, playedFrameCount,
                           {.bufferStartFrame = bufferStartFrame,
                            .frameCount = playedFrameCount,
                            .effectStartFrame = effectStartPos,
                            .effectEndFrame = effectEndPos,
                            .targetChannels = processorChannels});
    }

    if (playedFrameCount > 0)
    {
        meterAccumulator.mergeInto(meterLevels);
    }

    return playedAnyFrame;
}

void cupuacu::audio::callback_core::recordInputIntoChunks(
    const float *input, const unsigned long framesPerBuffer,
    const uint8_t recordingChannels, int64_t &recordingPosition,
    void *chunkSinkUser, const ChunkPushFn chunkPushFn,
    StereoMeterLevels &meterLevels)
{
    if (!input || recordingChannels == 0 || recordingChannels > 2 || !chunkPushFn)
    {
        return;
    }

    unsigned long frameOffset = 0;
    uint64_t recordedFrameCount = 0;
    cupuacu::audio::StereoMeterAccumulator meterAccumulator;
    while (frameOffset < framesPerBuffer)
    {
        cupuacu::audio::RecordedChunk chunk{};
        chunk.startFrame = recordingPosition;
        chunk.channelCount = recordingChannels;
        chunk.frameCount = static_cast<uint32_t>(std::min<unsigned long>(
            cupuacu::audio::kRecordedChunkFrames, framesPerBuffer - frameOffset));

        for (uint32_t frame = 0; frame < chunk.frameCount; ++frame)
        {
            const std::size_t sourceBase =
                static_cast<std::size_t>(frameOffset + frame) *
                static_cast<std::size_t>(recordingChannels);
            const float inL = input[sourceBase];
            const float inR = recordingChannels > 1 ? input[sourceBase + 1] : inL;

            const std::size_t targetBase =
                static_cast<std::size_t>(frame) *
                cupuacu::audio::kMaxRecordedChannels;
            chunk.interleavedSamples[targetBase] = inL;
            chunk.interleavedSamples[targetBase + 1] = inR;

            meterAccumulator.addFrame(inL, inR);
        }

        chunkPushFn(chunkSinkUser, chunk);
        recordingPosition += static_cast<int64_t>(chunk.frameCount);
        frameOffset += chunk.frameCount;
        recordedFrameCount += chunk.frameCount;
    }

    if (recordedFrameCount > 0)
    {
        meterAccumulator.mergeInto(meterLevels);
    }
}
