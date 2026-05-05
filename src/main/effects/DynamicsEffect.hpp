#pragma once

#include "EffectDialogWindow.hpp"
#include "EffectSettings.hpp"
#include "EffectTargeting.hpp"

#include "LongTask.hpp"
#include "audio/AudioProcessor.hpp"
#include "actions/Undoable.hpp"
#include "actions/audio/SampleStore.hpp"
#include "gui/MainViewAccess.hpp"
#include "gui/Waveform.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace cupuacu::actions::effects
{
    bool queueDynamics(cupuacu::State *state,
                       const ::cupuacu::effects::DynamicsSettings &settings);
}

namespace cupuacu::effects
{
    class DynamicsUndoable : public cupuacu::actions::Undoable
    {
    public:
        DynamicsUndoable(cupuacu::State *stateToUse,
                         const DynamicsSettings &settingsToUse)
            : Undoable(stateToUse),
              tabIndex(stateToUse ? stateToUse->activeTabIndex : -1),
              settings(settingsToUse)
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

        DynamicsUndoable(cupuacu::State *stateToUse, const int tabIndexToUse,
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

        DynamicsUndoable(
            cupuacu::State *stateToUse, const int tabIndexToUse,
            const DynamicsSettings &settingsToUse,
            const int64_t startFrameToUse, const int64_t frameCountToUse,
            std::vector<int64_t> targetChannelsToUse,
            undo::UndoStore::SampleMatrixHandle oldSamplesHandleToUse,
            undo::UndoStore::SampleMatrixHandle newSamplesHandleToUse)
            : Undoable(stateToUse), settings(settingsToUse),
              startFrame(startFrameToUse), frameCount(frameCountToUse),
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
                                          pendingOldSamples, "dynamics-old");
            pendingOldSamples.reset();
            applySamples(cupuacu::actions::audio::detail::materializeSampleMatrix(
                *session, pendingNewSamples, newSamplesHandle, "dynamics-new"));
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
                                          pendingNewSamples, "dynamics-new");
            pendingNewSamples.reset();
            applySamples(cupuacu::actions::audio::detail::materializeSampleMatrix(
                *session, pendingOldSamples, oldSamplesHandle, "dynamics-old"));
        }

        std::string getUndoDescription() override
        {
            return "Dynamics";
        }

        std::string getRedoDescription() override
        {
            return "Dynamics";
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
                {"kind", "dynamics"},
                {"settings",
                 {{"thresholdPercent", settings.thresholdPercent},
                  {"ratioIndex", settings.ratioIndex}}},
                {"startFrame", startFrame},
                {"frameCount", frameCount},
                {"targetChannels", targetChannels},
                {"oldSamplesHandle", oldSamplesHandle.path.string()},
                {"newSamplesHandle", newSamplesHandle.path.string()},
            };
        }

        [[nodiscard]] cupuacu::file::OverwritePreservationMutation
        overwritePreservationMutation() const override
        {
            return cupuacu::file::OverwritePreservationMutationHelper::compatible();
        }

    public:
        static double getRatioForSettings(const DynamicsSettings &settings)
        {
            switch (settings.ratioIndex)
            {
            case 0:
                return 2.0;
            case 1:
                return 4.0;
            case 2:
                return 8.0;
            default:
                return std::numeric_limits<double>::infinity();
            }
        }

        static float processSampleValue(const DynamicsSettings &settings,
                                        const float sample)
        {
            const double threshold = settings.thresholdPercent / 100.0;
            if (threshold <= 0.0)
            {
                return 0.0f;
            }

            const double magnitude = std::fabs(sample);
            if (magnitude <= threshold)
            {
                return sample;
            }

            const double ratio = getRatioForSettings(settings);
            double compressedMagnitude = threshold;
            if (std::isfinite(ratio))
            {
                compressedMagnitude += (magnitude - threshold) / ratio;
            }
            return static_cast<float>(std::copysign(compressedMagnitude, sample));
        }

        [[nodiscard]] int getTabIndex() const
        {
            return tabIndex;
        }

        [[nodiscard]] const DynamicsSettings &getSettings() const
        {
            return settings;
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

    private:
        DynamicsSettings settings{};
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

            if (!getTargetRange(state, startFrame, frameCount))
            {
                return;
            }
            targetChannels = ::cupuacu::effects::getTargetChannels(state);
            if (targetChannels.empty())
            {
                return;
            }

            auto &session =
                state->tabs[static_cast<std::size_t>(tabIndex)].session;
            auto &document = session.document;
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
                    const float oldValue =
                        document.getSample(channel, startFrame + frame);
                    oldChannel[static_cast<size_t>(frame)] = oldValue;
                    newChannel[static_cast<size_t>(frame)] =
                        processSampleValue(settings, oldValue);
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

    inline void performDynamics(cupuacu::State *state,
                                const DynamicsSettings &settings)
    {
        if (!state ||
            state->getActiveDocumentSession().document.getFrameCount() <= 0 ||
            state->getActiveDocumentSession().document.getChannelCount() <= 0)
        {
            return;
        }

        const bool hasSelection = state->getActiveDocumentSession().selection.isActive();
        if (hasSelection &&
            state->getActiveDocumentSession().selection.getLengthInt() <= 0)
        {
            return;
        }

        cupuacu::actions::effects::queueDynamics(state, settings);
    }

    class DynamicsPreviewProcessor : public cupuacu::audio::AudioProcessor
    {
    public:
        explicit DynamicsPreviewProcessor(const DynamicsSettings &settingsToUse)
        {
            updateSettings(settingsToUse);
        }

        void updateSettings(const DynamicsSettings &settingsToUse)
        {
            thresholdPercent.store(settingsToUse.thresholdPercent,
                                   std::memory_order_release);
            ratioIndex.store(settingsToUse.ratioIndex,
                             std::memory_order_release);
        }

        void process(float *interleavedStereo, const unsigned long frameCount,
                     const cupuacu::audio::AudioProcessContext &context) const override
        {
            if (!interleavedStereo || frameCount == 0)
            {
                return;
            }

            DynamicsSettings settings{};
            settings.thresholdPercent =
                thresholdPercent.load(std::memory_order_acquire);
            settings.ratioIndex = ratioIndex.load(std::memory_order_acquire);
            for (unsigned long i = 0; i < frameCount; ++i)
            {
                float *frame = interleavedStereo + i * 2;
                if (context.targetChannels != cupuacu::SelectedChannels::RIGHT)
                {
                    frame[0] =
                        DynamicsUndoable::processSampleValue(settings, frame[0]);
                }
                if (context.targetChannels != cupuacu::SelectedChannels::LEFT)
                {
                    frame[1] =
                        DynamicsUndoable::processSampleValue(settings, frame[1]);
                }
            }
        }

    private:
        std::atomic<double> thresholdPercent{50.0};
        std::atomic<int> ratioIndex{1};
    };

    class DynamicsPreviewSession : public EffectPreviewSession<DynamicsSettings>
    {
    public:
        explicit DynamicsPreviewSession(const DynamicsSettings &settings)
            : processor(std::make_shared<DynamicsPreviewProcessor>(settings))
        {
        }

        std::shared_ptr<const cupuacu::audio::AudioProcessor>
        getProcessor() const override
        {
            return processor;
        }

        void updateSettings(const DynamicsSettings &settings) override
        {
            processor->updateSettings(settings);
        }

    private:
        std::shared_ptr<DynamicsPreviewProcessor> processor;
    };

    class DynamicsDialog
    {
    public:
        explicit DynamicsDialog(cupuacu::State *stateToUse);

        bool isOpen() const
        {
            return dialog && dialog->isOpen();
        }
        void raise() const
        {
            if (dialog)
            {
                dialog->raise();
            }
        }
        cupuacu::gui::Window *getWindow() const
        {
            return dialog ? dialog->getWindow() : nullptr;
        }

    private:
        std::unique_ptr<EffectDialogWindow<DynamicsSettings>> dialog;
    };
} // namespace cupuacu::effects
