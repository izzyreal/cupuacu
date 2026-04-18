#pragma once

#include "EffectTargeting.hpp"

#include "actions/Undoable.hpp"
#include "gui/MainViewAccess.hpp"
#include "gui/Waveform.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace cupuacu::effects
{
    class ReverseUndoable : public cupuacu::actions::Undoable
    {
    public:
        explicit ReverseUndoable(cupuacu::State *stateToUse)
            : Undoable(stateToUse)
        {
            captureTargetsAndSamples();
            updateGui = [this]
            {
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

        void captureTargetsAndSamples()
        {
            if (!state)
            {
                return;
            }

            auto &document = state->getActiveDocumentSession().document;
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

            auto &document = state->getActiveDocumentSession().document;
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
                document.getWaveformCache(channel).invalidateSamples(
                    startFrame, startFrame + frameCount - 1);
            }
            document.updateWaveformCache();
        }
    };

    inline void performReverse(cupuacu::State *state)
    {
        if (!state)
        {
            return;
        }

        const auto &document = state->getActiveDocumentSession().document;
        if (document.getFrameCount() <= 0 || document.getChannelCount() <= 0)
        {
            return;
        }

        const auto &selection = state->getActiveDocumentSession().selection;
        if (selection.isActive() && selection.getLengthInt() <= 0)
        {
            return;
        }

        state->addAndDoUndoable(std::make_shared<ReverseUndoable>(state));
    }
} // namespace cupuacu::effects
