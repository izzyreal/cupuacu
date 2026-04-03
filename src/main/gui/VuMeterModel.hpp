#pragma once

#include "audio/MeterFrame.hpp"
#include "gui/VuMeterScale.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

namespace cupuacu::gui
{
    struct VuMeterChannelDisplay
    {
        float level = 0.0f;
        float hold = 0.0f;
    };

    class VuMeterModel
    {
    public:
        void setNumChannels(const int n)
        {
            channelStates.assign(std::max(0, n), {});
        }

        void setScale(const VuMeterScale scale)
        {
            if (!lastScale.has_value() || *lastScale != scale)
            {
                lastScale = scale;
                reset();
            }
        }

        void reset()
        {
            for (auto &state : channelStates)
            {
                state = {};
            }
        }

        [[nodiscard]] VuMeterChannelDisplay advanceChannel(
            const int channel, const audio::MeterFrame &frame, const bool isDecaying,
            const float dt = 1.0f / 60.0f)
        {
            if (channel < 0 ||
                channel >= static_cast<int>(channelStates.size()))
            {
                return {};
            }

            const VuMeterScale scale =
                lastScale.value_or(VuMeterScale::PeakDbfs);
            const float levelInput =
                scale == VuMeterScale::PeakDbfs ? frame.peak : frame.rms;
            const float normalizedLevel =
                levelInput > 0.0f ? normalizePeakForVuMeter(levelInput, scale)
                                  : 0.0f;
            ChannelState &state = channelStates[channel];
            const float releaseTimeSec =
                isDecaying ? decayReleaseTimeSec : normalReleaseTimeSec;
            const float alphaAttack = 1.0f - std::exp(-dt / attackTimeSec);
            const float alphaRelease = 1.0f - std::exp(-dt / releaseTimeSec);

            if (normalizedLevel > state.displayedLevel)
            {
                state.displayedLevel +=
                    (normalizedLevel - state.displayedLevel) * alphaAttack;
            }
            else
            {
                state.displayedLevel +=
                    (normalizedLevel - state.displayedLevel) * alphaRelease;
            }

            if (normalizedLevel > state.holdLevel)
            {
                state.holdLevel = normalizedLevel;
                state.holdFrames = holdTimeFrames;
            }
            else if (state.holdFrames > 0)
            {
                --state.holdFrames;
            }
            else
            {
                state.holdLevel = std::max(
                    0.0f, state.holdLevel - peakHoldDecayPerSecond * dt);
                if (state.holdLevel < state.displayedLevel)
                {
                    state.holdLevel = state.displayedLevel;
                }
            }

            return {.level = state.displayedLevel, .hold = state.holdLevel};
        }

        [[nodiscard]] bool isAtRest() const
        {
            for (const auto &state : channelStates)
            {
                if (state.displayedLevel > 1e-6f || state.holdLevel > 1e-6f)
                {
                    return false;
                }
            }

            return true;
        }

    private:
        struct ChannelState
        {
            float displayedLevel = 0.0f;
            float holdLevel = 0.0f;
            int holdFrames = 0;
        };

        std::vector<ChannelState> channelStates;
        std::optional<VuMeterScale> lastScale;

        static constexpr float attackTimeSec = 0.012f;
        static constexpr float normalReleaseTimeSec = 0.09f;
        static constexpr float decayReleaseTimeSec = 0.02f;
        static constexpr int holdTimeFrames = 30;
        static constexpr float peakHoldDecayPerSecond = 0.6f;
    };
} // namespace cupuacu::gui
