#include "AudioCallbackCore.hpp"

#include <algorithm>
#include <cmath>

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
    const cupuacu::Document *document, const bool selectionIsActive,
    const cupuacu::SelectedChannels selectedChannels, int64_t &playbackPosition,
    uint64_t &playbackStartPos, uint64_t &playbackEndPos,
    const bool playbackLoopEnabled, bool &playbackHasPendingSwitch,
    uint64_t &playbackPendingStartPos, uint64_t &playbackPendingEndPos,
    bool &isPlaying, float *out,
    const unsigned long framesPerBuffer, float &peakLeft, float &peakRight)
{
    if (!out)
    {
        return false;
    }

    if (!document || (document->getChannelCount() != 1 &&
                      document->getChannelCount() != 2))
    {
        writeSilenceToOutput(out, framesPerBuffer);
        return false;
    }

    const auto docBuf = document->getAudioBuffer();
    const auto chBufL = docBuf->getImmutableChannelData(0);
    const auto chBufR =
        docBuf->getImmutableChannelData(docBuf->getChannelCount() == 2 ? 1 : 0);

    const bool shouldPlayChannelL =
        !selectionIsActive || selectedChannels == cupuacu::SelectedChannels::BOTH ||
        selectedChannels == cupuacu::SelectedChannels::LEFT;

    const bool shouldPlayChannelR =
        !selectionIsActive || selectedChannels == cupuacu::SelectedChannels::BOTH ||
        selectedChannels == cupuacu::SelectedChannels::RIGHT;

    bool playedAnyFrame = false;
    for (unsigned long i = 0; i < framesPerBuffer; ++i)
    {
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

        const float outL = shouldPlayChannelL ? chBufL[playbackPosition] : 0.0f;
        const float outR = shouldPlayChannelR ? chBufR[playbackPosition] : 0.0f;

        *out++ = outL;
        *out++ = outR;

        peakLeft = std::max(peakLeft, std::abs(outL));
        peakRight = std::max(peakRight, std::abs(outR));
        ++playbackPosition;
        playedAnyFrame = true;
    }

    return playedAnyFrame;
}

void cupuacu::audio::callback_core::recordInputIntoChunks(
    const float *input, const unsigned long framesPerBuffer,
    const uint8_t recordingChannels, int64_t &recordingPosition,
    void *chunkSinkUser, const ChunkPushFn chunkPushFn, float &peakLeft,
    float &peakRight)
{
    if (!input || recordingChannels == 0 || recordingChannels > 2 || !chunkPushFn)
    {
        return;
    }

    unsigned long frameOffset = 0;
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

            peakLeft = std::max(peakLeft, std::abs(inL));
            peakRight = std::max(peakRight, std::abs(inR));
        }

        chunkPushFn(chunkSinkUser, chunk);
        recordingPosition += static_cast<int64_t>(chunk.frameCount);
        frameOffset += chunk.frameCount;
    }
}
