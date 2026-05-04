#pragma once

#include "EffectTargeting.hpp"

#include "actions/Undoable.hpp"
#include "gui/MainViewAccess.hpp"
#include "gui/Waveform.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace cupuacu::actions::effects
{
    bool queueReverse(cupuacu::State *state);
}

namespace cupuacu::effects
{
    class ReverseUndoable : public cupuacu::actions::Undoable
    {
    public:
        explicit ReverseUndoable(cupuacu::State *stateToUse)
            : Undoable(stateToUse), tabIndex(stateToUse ? stateToUse->activeTabIndex
                                                        : -1)
        {
            captureTargetsAndSamples();
            updateGui = [this]
            {
                if (!state || state->activeTabIndex != tabIndex)
                {
                    return;
                }
                cupuacu::gui::Waveform::updateAllSamplePoints(state);
                cupuacu::gui::Waveform::setAllWaveformsDirty(state);
                cupuacu::gui::requestMainViewRefresh(state);
            };
        }

        ReverseUndoable(cupuacu::State *stateToUse, const int tabIndexToUse,
                        const int64_t startFrameToUse,
                        std::vector<int64_t> targetChannelsToUse,
                        std::vector<std::vector<float>> oldSamplesToUse,
                        std::vector<std::vector<float>> newSamplesToUse)
            : Undoable(stateToUse),
              startFrame(startFrameToUse),
              frameCount(oldSamplesToUse.empty()
                             ? 0
                             : static_cast<int64_t>(oldSamplesToUse.front().size())),
              targetChannels(std::move(targetChannelsToUse)),
              oldSamples(std::move(oldSamplesToUse)),
              newSamples(std::move(newSamplesToUse)),
              tabIndex(tabIndexToUse)
        {
            updateGui = [this]
            {
                if (!state || state->activeTabIndex != tabIndex)
                {
                    return;
                }
                cupuacu::gui::Waveform::updateAllSamplePoints(state);
                cupuacu::gui::Waveform::setAllWaveformsDirty(state);
                cupuacu::gui::requestMainViewRefresh(state);
            };
        }

        void redo() override
        {
            applySamples(newSamples);
        }

        void undo() override
        {
            applySamples(oldSamples);
        }

        std::string getUndoDescription() override
        {
            return "Reverse";
        }

        std::string getRedoDescription() override
        {
            return "Reverse";
        }

        [[nodiscard]] cupuacu::file::OverwritePreservationMutation
        overwritePreservationMutation() const override
        {
            return cupuacu::file::OverwritePreservationMutationHelper::compatible();
        }

    private:
        int64_t startFrame = 0;
        int64_t frameCount = 0;
        std::vector<int64_t> targetChannels;
        std::vector<std::vector<float>> oldSamples;
        std::vector<std::vector<float>> newSamples;
        int tabIndex = -1;

        void captureTargetsAndSamples()
        {
            if (!state)
            {
                return;
            }

            if (tabIndex < 0 || tabIndex >= static_cast<int>(state->tabs.size()))
            {
                return;
            }

            auto &session =
                state->tabs[static_cast<std::size_t>(tabIndex)].session;
            auto &document = session.document;
            if (document.getChannelCount() <= 0)
            {
                return;
            }

            if (!getTargetRange(state, startFrame, frameCount))
            {
                return;
            }

            targetChannels = getTargetChannels(state);
            if (targetChannels.empty())
            {
                return;
            }

            oldSamples.resize(targetChannels.size());
            newSamples.resize(targetChannels.size());

            for (size_t channelIndex = 0; channelIndex < targetChannels.size();
                 ++channelIndex)
            {
                const int64_t channel = targetChannels[channelIndex];
                auto &oldChannel = oldSamples[channelIndex];
                auto &newChannel = newSamples[channelIndex];
                oldChannel.resize(static_cast<size_t>(frameCount));
                newChannel.resize(static_cast<size_t>(frameCount));

                for (int64_t frame = 0; frame < frameCount; ++frame)
                {
                    oldChannel[static_cast<size_t>(frame)] =
                        document.getSample(channel, startFrame + frame);
                }

                for (int64_t frame = 0; frame < frameCount; ++frame)
                {
                    newChannel[static_cast<size_t>(frame)] =
                        oldChannel[static_cast<size_t>(frameCount - 1 - frame)];
                }
            }
        }

        void applySamples(const std::vector<std::vector<float>> &samples) const
        {
            if (!state || frameCount <= 0 || targetChannels.empty())
            {
                return;
            }

            if (tabIndex < 0 || tabIndex >= static_cast<int>(state->tabs.size()))
            {
                return;
            }

            auto &session =
                state->tabs[static_cast<std::size_t>(tabIndex)].session;
            auto &document = session.document;
            for (size_t channelIndex = 0; channelIndex < targetChannels.size();
                 ++channelIndex)
            {
                const int64_t channel = targetChannels[channelIndex];
                for (int64_t frame = 0; frame < frameCount; ++frame)
                {
                    document.setSample(
                        channel, startFrame + frame,
                        samples[channelIndex][static_cast<size_t>(frame)], true);
                }
                session.getWaveformCache(channel).invalidateSamples(
                    startFrame, startFrame + frameCount - 1);
            }
            session.updateWaveformCache();
        }
    };

    inline void performReverse(cupuacu::State *state)
    {
        cupuacu::actions::effects::queueReverse(state);
    }
} // namespace cupuacu::effects
