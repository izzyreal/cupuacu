#pragma once

#include "State.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace cupuacu::effects
{
    inline std::vector<int64_t> getTargetChannels(cupuacu::State *state)
    {
        std::vector<int64_t> targetChannels;
        if (!state)
        {
            return targetChannels;
        }

        auto &session = state->getActiveDocumentSession();
        auto &document = session.document;
        const int64_t channelCount = document.getChannelCount();
        if (channelCount <= 0)
        {
            return targetChannels;
        }

        if (!session.selection.isActive())
        {
            for (int64_t channel = 0; channel < channelCount; ++channel)
            {
                targetChannels.push_back(channel);
            }
            return targetChannels;
        }

        const SelectedChannels selectedChannels =
            state->getActiveViewState().selectedChannels;

        if (channelCount <= 1 || selectedChannels == SelectedChannels::BOTH)
        {
            for (int64_t channel = 0; channel < channelCount; ++channel)
            {
                targetChannels.push_back(channel);
            }
        }
        else if (selectedChannels == SelectedChannels::LEFT)
        {
            targetChannels.push_back(0);
        }
        else
        {
            targetChannels.push_back(std::min<int64_t>(1, channelCount - 1));
        }

        return targetChannels;
    }

    inline bool getTargetRange(cupuacu::State *state, int64_t &startFrameOut,
                               int64_t &frameCountOut)
    {
        startFrameOut = 0;
        frameCountOut = 0;
        if (!state)
        {
            return false;
        }

        auto &session = state->getActiveDocumentSession();
        auto &document = session.document;
        if (document.getFrameCount() <= 0 || document.getChannelCount() <= 0)
        {
            return false;
        }

        if (session.selection.isActive())
        {
            startFrameOut = session.selection.getStartInt();
            frameCountOut = session.selection.getLengthInt();
        }
        else
        {
            frameCountOut = document.getFrameCount();
        }

        return frameCountOut > 0;
    }

    inline float computeTargetPeakAbsolute(cupuacu::State *state)
    {
        int64_t startFrame = 0;
        int64_t frameCount = 0;
        if (!getTargetRange(state, startFrame, frameCount))
        {
            return 0.0f;
        }

        const auto targetChannels = getTargetChannels(state);
        if (targetChannels.empty())
        {
            return 0.0f;
        }

        auto &document = state->getActiveDocumentSession().document;
        float peak = 0.0f;
        for (const auto channel : targetChannels)
        {
            for (int64_t frame = 0; frame < frameCount; ++frame)
            {
                peak = std::max(
                    peak,
                    std::fabs(document.getSample(channel, startFrame + frame)));
            }
        }
        return peak;
    }

    inline bool getPreviewRange(cupuacu::State *state, uint64_t &startOut,
                                uint64_t &endOut)
    {
        startOut = 0;
        endOut = 0;

        int64_t startFrame = 0;
        int64_t frameCount = 0;
        if (!getTargetRange(state, startFrame, frameCount))
        {
            return false;
        }

        startOut = static_cast<uint64_t>(std::max<int64_t>(0, startFrame));
        endOut = startOut + static_cast<uint64_t>(frameCount);
        return endOut > startOut;
    }

    inline cupuacu::SelectedChannels getPreviewSelectedChannels(
        cupuacu::State *state)
    {
        if (!state)
        {
            return cupuacu::SelectedChannels::BOTH;
        }

        const auto &document = state->getActiveDocumentSession().document;
        if (document.getChannelCount() <= 1)
        {
            return cupuacu::SelectedChannels::BOTH;
        }

        if (!state->getActiveDocumentSession().selection.isActive())
        {
            return cupuacu::SelectedChannels::BOTH;
        }

        return state->getActiveViewState().selectedChannels;
    }
} // namespace cupuacu::effects
