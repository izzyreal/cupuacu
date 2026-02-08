#pragma once

#include "DocumentSession.hpp"

#include <algorithm>
#include <cstdint>

namespace cupuacu::playback
{
    struct Range
    {
        uint64_t start = 0;
        uint64_t end = 0;
    };

    inline Range computeRangeForPlay(const cupuacu::DocumentSession &session,
                                     const bool loopPlaybackEnabled)
    {
        const uint64_t totalFrames =
            static_cast<uint64_t>(std::max<int64_t>(
                int64_t{0}, session.document.getFrameCount()));

        uint64_t start = 0;
        uint64_t end = totalFrames;

        if (session.selection.isActive())
        {
            start = static_cast<uint64_t>(
                std::max<int64_t>(int64_t{0}, session.selection.getStartInt()));
            end = static_cast<uint64_t>(
                std::max<int64_t>(static_cast<int64_t>(start),
                                  session.selection.getEndInt() + 1));
        }
        else if (loopPlaybackEnabled)
        {
            start = static_cast<uint64_t>(
                std::clamp<int64_t>(session.cursor, int64_t{0},
                                    static_cast<int64_t>(totalFrames)));
        }
        else
        {
            start = static_cast<uint64_t>(
                std::clamp<int64_t>(session.cursor, int64_t{0},
                                    static_cast<int64_t>(totalFrames)));
        }

        start = std::min(start, totalFrames);
        end = std::min(std::max(end, start), totalFrames);
        return {.start = start, .end = end};
    }

    inline Range computeRangeForLiveUpdate(
        const cupuacu::DocumentSession &session, const bool loopPlaybackEnabled,
        const uint64_t fallbackStart, const uint64_t fallbackEnd)
    {
        const uint64_t totalFrames =
            static_cast<uint64_t>(std::max<int64_t>(
                int64_t{0}, session.document.getFrameCount()));

        uint64_t start = fallbackStart;
        uint64_t end = fallbackEnd > 0 ? fallbackEnd : totalFrames;

        if (session.selection.isActive())
        {
            start = static_cast<uint64_t>(
                std::max<int64_t>(int64_t{0}, session.selection.getStartInt()));
            end = static_cast<uint64_t>(
                std::max<int64_t>(static_cast<int64_t>(start),
                                  session.selection.getEndInt() + 1));
        }
        else if (loopPlaybackEnabled)
        {
            start = 0;
            end = totalFrames;
        }

        start = std::min(start, totalFrames);
        end = std::min(std::max(end, start), totalFrames);
        return {.start = start, .end = end};
    }
} // namespace cupuacu::playback
