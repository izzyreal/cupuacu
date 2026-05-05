#pragma once

#include "EffectTargeting.hpp"

#include "actions/Undoable.hpp"
#include "actions/audio/SampleStore.hpp"
#include "gui/MainViewAccess.hpp"
#include "gui/Waveform.hpp"

#include <algorithm>
#include <memory>
#include <optional>
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
              pendingOldSamples(std::move(oldSamplesToUse)),
              pendingNewSamples(std::move(newSamplesToUse)),
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

        ReverseUndoable(
            cupuacu::State *stateToUse, const int tabIndexToUse,
            const int64_t startFrameToUse, const int64_t frameCountToUse,
            std::vector<int64_t> targetChannelsToUse,
            undo::UndoStore::SampleMatrixHandle oldSamplesHandleToUse,
            undo::UndoStore::SampleMatrixHandle newSamplesHandleToUse)
            : Undoable(stateToUse), startFrame(startFrameToUse),
              frameCount(frameCountToUse),
              targetChannels(std::move(targetChannelsToUse)),
              oldSamplesHandle(std::move(oldSamplesHandleToUse)),
              newSamplesHandle(std::move(newSamplesHandleToUse)),
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
            auto *session = sessionForTab();
            if (!session)
            {
                return;
            }

            oldSamplesHandle = cupuacu::actions::audio::detail::
                storeSampleMatrixIfNeeded(*session, oldSamplesHandle,
                                          pendingOldSamples, "reverse-old");
            pendingOldSamples.reset();
            applySamples(cupuacu::actions::audio::detail::materializeSampleMatrix(
                *session, pendingNewSamples, newSamplesHandle, "reverse-new"));
        }

        void undo() override
        {
            auto *session = sessionForTab();
            if (!session)
            {
                return;
            }

            newSamplesHandle = cupuacu::actions::audio::detail::
                storeSampleMatrixIfNeeded(*session, newSamplesHandle,
                                          pendingNewSamples, "reverse-new");
            pendingNewSamples.reset();
            applySamples(cupuacu::actions::audio::detail::materializeSampleMatrix(
                *session, pendingOldSamples, oldSamplesHandle, "reverse-old"));
        }

        std::string getUndoDescription() override
        {
            return "Reverse";
        }

        std::string getRedoDescription() override
        {
            return "Reverse";
        }

        [[nodiscard]] bool canPersistForRestart() const override
        {
            return !oldSamplesHandle.empty() && !newSamplesHandle.empty();
        }

        [[nodiscard]] std::optional<nlohmann::json>
        serializeForRestart() const override
        {
            if (!canPersistForRestart())
            {
                return std::nullopt;
            }
            return nlohmann::json{
                {"kind", "reverse"},
                {"startFrame", startFrame},
                {"frameCount", frameCount},
                {"targetChannels", targetChannels},
                {"oldSamplesHandle", oldSamplesHandle.path.string()},
                {"newSamplesHandle", newSamplesHandle.path.string()},
            };
        }

        [[nodiscard]] int getTabIndex() const
        {
            return tabIndex;
        }

        [[nodiscard]] int64_t getStartFrame() const
        {
            return startFrame;
        }

        [[nodiscard]] int64_t getFrameCount() const
        {
            return frameCount;
        }

        [[nodiscard]] const std::vector<int64_t> &getTargetChannels() const
        {
            return targetChannels;
        }

        [[nodiscard]] const undo::UndoStore::SampleMatrixHandle &
        getOldSamplesHandle() const
        {
            return oldSamplesHandle;
        }

        [[nodiscard]] const undo::UndoStore::SampleMatrixHandle &
        getNewSamplesHandle() const
        {
            return newSamplesHandle;
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
        std::optional<cupuacu::actions::audio::detail::SampleMatrix>
            pendingOldSamples;
        std::optional<cupuacu::actions::audio::detail::SampleMatrix>
            pendingNewSamples;
        undo::UndoStore::SampleMatrixHandle oldSamplesHandle;
        undo::UndoStore::SampleMatrixHandle newSamplesHandle;
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

            targetChannels = ::cupuacu::effects::getTargetChannels(state);
            if (targetChannels.empty())
            {
                return;
            }

            cupuacu::actions::audio::detail::SampleMatrix oldSamples;
            cupuacu::actions::audio::detail::SampleMatrix newSamples;
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

            pendingOldSamples = std::move(oldSamples);
            pendingNewSamples = std::move(newSamples);
        }

        [[nodiscard]] cupuacu::DocumentSession *sessionForTab() const
        {
            if (!state)
            {
                return nullptr;
            }

            if (tabIndex < 0 || tabIndex >= static_cast<int>(state->tabs.size()))
            {
                return nullptr;
            }

            return &state->tabs[static_cast<std::size_t>(tabIndex)].session;
        }

        void applySamples(
            const cupuacu::actions::audio::detail::SampleMatrix &samples) const
        {
            if (!state || frameCount <= 0 || targetChannels.empty())
            {
                return;
            }

            auto *session = sessionForTab();
            if (!session)
            {
                return;
            }

            auto &document = session->document;
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
                session->getWaveformCache(channel).invalidateSamples(
                    startFrame, startFrame + frameCount - 1);
            }
            session->updateWaveformCache();
        }
    };

    inline void performReverse(cupuacu::State *state)
    {
        cupuacu::actions::effects::queueReverse(state);
    }
} // namespace cupuacu::effects
