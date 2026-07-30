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

bool cupuacu::audio::callback_core::monitorInputToOutput(
    const float *input, const uint8_t inputChannels, float *out,
    const unsigned long framesPerBuffer, StereoMeterLevels &meterLevels)
{
    if (!input || !out || (inputChannels != 1 && inputChannels != 2))
    {
        return false;
    }

    cupuacu::audio::StereoMeterAccumulator meterAccumulator;
    for (unsigned long frame = 0; frame < framesPerBuffer; ++frame)
    {
        const std::size_t inputBase =
            static_cast<std::size_t>(frame) * inputChannels;
        const float left = input[inputBase];
        const float right = inputChannels == 2 ? input[inputBase + 1] : left;
        const std::size_t outputBase = static_cast<std::size_t>(frame) * 2;
        out[outputBase] = left;
        out[outputBase + 1] = right;
        meterAccumulator.addFrame(left, right);
    }
    meterAccumulator.mergeInto(meterLevels);
    return framesPerBuffer > 0;
}

bool cupuacu::audio::callback_core::measureInput(
    const float *input, const uint8_t inputChannels,
    const unsigned long framesPerBuffer, StereoMeterLevels &meterLevels)
{
    if (!input || (inputChannels != 1 && inputChannels != 2))
    {
        return false;
    }

    cupuacu::audio::StereoMeterAccumulator meterAccumulator;
    for (unsigned long frame = 0; frame < framesPerBuffer; ++frame)
    {
        const std::size_t inputBase =
            static_cast<std::size_t>(frame) * inputChannels;
        const float left = input[inputBase];
        const float right = inputChannels == 2 ? input[inputBase + 1] : left;
        meterAccumulator.addFrame(left, right);
    }
    meterAccumulator.mergeInto(meterLevels);
    return framesPerBuffer > 0;
}

bool cupuacu::audio::callback_core::fillOutputBuffer(
    const std::shared_ptr<cupuacu::audio::AudioBuffer> &buffer,
    const uint8_t channelCount, const bool selectionIsActive,
    const cupuacu::SelectedChannels selectedChannels, int64_t &playbackPosition,
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
    const auto chBufR =
        buffer->getImmutableChannelData(channelCount == 2 ? 1 : 0);

    const bool shouldPlayChannelL =
        !selectionIsActive ||
        selectedChannels == cupuacu::SelectedChannels::BOTH ||
        selectedChannels == cupuacu::SelectedChannels::LEFT;

    const bool shouldPlayChannelR =
        !selectionIsActive ||
        selectedChannels == cupuacu::SelectedChannels::BOTH ||
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

bool cupuacu::audio::callback_core::recordInputIntoChunks(
    const float *input, const unsigned long framesPerBuffer,
    const uint8_t inputChannels, const uint8_t recordingChannels,
    int64_t &recordingPosition, void *chunkSinkUser,
    const ChunkPushFn chunkPushFn, StereoMeterLevels &meterLevels)
{
    if (!input || inputChannels == 0 || inputChannels > 2 ||
        recordingChannels == 0 || recordingChannels > 2 || !chunkPushFn)
    {
        return false;
    }

    unsigned long frameOffset = 0;
    uint64_t recordedFrameCount = 0;
    cupuacu::audio::StereoMeterAccumulator meterAccumulator;
    while (frameOffset < framesPerBuffer)
    {
        cupuacu::audio::RecordedChunk chunk{};
        chunk.startFrame = recordingPosition;
        chunk.channelCount = recordingChannels;
        chunk.frameCount = static_cast<uint32_t>(
            std::min<unsigned long>(cupuacu::audio::kRecordedChunkFrames,
                                    framesPerBuffer - frameOffset));

        for (uint32_t frame = 0; frame < chunk.frameCount; ++frame)
        {
            const std::size_t sourceBase =
                static_cast<std::size_t>(frameOffset + frame) *
                static_cast<std::size_t>(inputChannels);
            float inL = input[sourceBase];
            float inR = inputChannels > 1 ? input[sourceBase + 1] : inL;
            if (recordingChannels == 1 && inputChannels == 2)
            {
                inL = 0.5f * (inL + inR);
                inR = inL;
            }

            const std::size_t targetBase = static_cast<std::size_t>(frame) *
                                           cupuacu::audio::kMaxRecordedChannels;
            chunk.interleavedSamples[targetBase] = inL;
            chunk.interleavedSamples[targetBase + 1] = inR;

            meterAccumulator.addFrame(inL, inR);
        }

        if (!chunkPushFn(chunkSinkUser, chunk))
        {
            if (recordedFrameCount > 0)
            {
                meterAccumulator.mergeInto(meterLevels);
            }
            return false;
        }
        recordingPosition += static_cast<int64_t>(chunk.frameCount);
        frameOffset += chunk.frameCount;
        recordedFrameCount += chunk.frameCount;
    }

    if (recordedFrameCount > 0)
    {
        meterAccumulator.mergeInto(meterLevels);
    }
    return true;
}
