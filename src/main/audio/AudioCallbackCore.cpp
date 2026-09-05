#include "AudioCallbackCore.hpp"
#include "MeterAccumulator.hpp"
#include "../playback/ReadAhead.hpp"

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
    const cupuacu::audio::AudioBuffer *buffer, const uint8_t channelCount,
    const bool selectionIsActive,
    const cupuacu::SelectedChannels selectedChannels, int64_t &playbackPosition,
    uint64_t &playbackStartPos, uint64_t &playbackEndPos,
    const bool playbackLoopEnabled, bool &playbackHasPendingSwitch,
    uint64_t &playbackPendingStartPos, uint64_t &playbackPendingEndPos,
    bool &isPlaying, float *out, const unsigned long framesPerBuffer,
    StereoMeterLevels &meterLevels,
    const cupuacu::audio::AudioProcessor *processor,
    const uint64_t effectStartPos, const uint64_t effectEndPos,
    const cupuacu::SelectedChannels processorChannels,
    playback::ReadAhead *readAhead, uint64_t *underrunFrames)
{
    if (!out)
    {
        return false;
    }

    if ((!buffer && !readAhead) || (channelCount != 1 && channelCount != 2))
    {
        writeSilenceToOutput(out, framesPerBuffer);
        return false;
    }

    const auto chBufL = buffer ? buffer->getImmutableChannelData(0)
                               : decltype(buffer->getImmutableChannelData(0)){};
    const auto chBufR =
        buffer ? buffer->getImmutableChannelData(channelCount == 2 ? 1 : 0)
               : decltype(chBufL){};

    if (readAhead)
    {
        readAhead->request(
            playbackPosition,
            playbackLoopEnabled
                ? static_cast<int64_t>(playbackHasPendingSwitch
                                           ? playbackPendingStartPos
                                           : playbackStartPos)
                : -1);
    }

    const bool shouldPlayChannelL =
        !selectionIsActive ||
        selectedChannels == cupuacu::SelectedChannels::BOTH ||
        selectedChannels == cupuacu::SelectedChannels::LEFT;

    const bool shouldPlayChannelR =
        !selectionIsActive ||
        selectedChannels == cupuacu::SelectedChannels::BOTH ||
        selectedChannels == cupuacu::SelectedChannels::RIGHT;

    float *segmentOutput = out;
    unsigned long segmentFrames = 0;
    bool playedAnyFrame = false;
    bool capturedBufferStart = false;
    int64_t bufferStartFrame = 0;
    unsigned long playedFrameCount = 0;
    const auto processSegment = [&]
    {
        if (segmentFrames && processor && effectEndPos > effectStartPos)
        {
            processor->process(segmentOutput, segmentFrames,
                               {.bufferStartFrame = bufferStartFrame,
                                .frameCount = segmentFrames,
                                .effectStartFrame = effectStartPos,
                                .effectEndFrame = effectEndPos,
                                .targetChannels = processorChannels});
        }
        segmentFrames = 0;
        segmentOutput = out;
        capturedBufferStart = false;
    };
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
            processSegment();
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

        float left = 0, right = 0;
        if (readAhead)
        {
            if (!readAhead->readStereo(playbackPosition, left, right))
            {
                // Preserve the source position; do not skip audio on a miss.
                // Initial buffering is measured separately from underruns.
                if (underrunFrames && readAhead->hasStarted())
                {
                    *underrunFrames += framesPerBuffer - i;
                }
                writeSilenceToOutput(out, framesPerBuffer - i);
                break;
            }
        }
        else
        {
            left = chBufL[playbackPosition];
            right = chBufR[playbackPosition];
        }
        const float outL = shouldPlayChannelL ? left : 0.0f;
        const float outR = shouldPlayChannelR ? right : 0.0f;

        *out++ = outL;
        *out++ = outR;

        meterAccumulator.addFrame(outL, outR);
        ++playbackPosition;
        ++playedFrameCount;
        ++segmentFrames;
        playedAnyFrame = true;
    }

    if (readAhead)
    {
        readAhead->endCallback();
    }

    processSegment();

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
