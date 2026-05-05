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
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace cupuacu::actions::effects
{
    bool queueAmplifyFade(cupuacu::State *state,
                          const ::cupuacu::effects::AmplifyFadeSettings &settings);
}

namespace cupuacu::effects
{
    class AmplifyFadeUndoable : public cupuacu::actions::Undoable
    {
    public:
        enum class Curve
        {
            Linear,
            Exponential,
            Logarithmic
        };

        AmplifyFadeUndoable(cupuacu::State *stateToUse,
                            const AmplifyFadeSettings &settingsToUse)
            : Undoable(stateToUse),
              tabIndex(stateToUse ? stateToUse->activeTabIndex : -1),
              settings(settingsToUse),
              curve(clampCurve(settings.curveIndex))
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

        AmplifyFadeUndoable(cupuacu::State *stateToUse, const int tabIndexToUse,
                            const int64_t startFrameToUse,
                            std::vector<int64_t> targetChannelsToUse,
                            std::vector<std::vector<float>> oldSamplesToUse,
                            std::vector<std::vector<float>> newSamplesToUse)
            : Undoable(stateToUse),
              tabIndex(tabIndexToUse),
              startFrame(startFrameToUse),
              frameCount(oldSamplesToUse.empty()
                             ? 0
                             : static_cast<int64_t>(oldSamplesToUse.front().size())),
              targetChannels(std::move(targetChannelsToUse)),
              pendingOldSamples(std::move(oldSamplesToUse)),
              pendingNewSamples(std::move(newSamplesToUse))
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

        AmplifyFadeUndoable(
            cupuacu::State *stateToUse, const int tabIndexToUse,
            const AmplifyFadeSettings &settingsToUse,
            const int64_t startFrameToUse, const int64_t frameCountToUse,
            std::vector<int64_t> targetChannelsToUse,
            undo::UndoStore::SampleMatrixHandle oldSamplesHandleToUse,
            undo::UndoStore::SampleMatrixHandle newSamplesHandleToUse)
            : Undoable(stateToUse), settings(settingsToUse),
              curve(clampCurve(settings.curveIndex)), tabIndex(tabIndexToUse),
              startFrame(startFrameToUse), frameCount(frameCountToUse),
              targetChannels(std::move(targetChannelsToUse)),
              oldSamplesHandle(std::move(oldSamplesHandleToUse)),
              newSamplesHandle(std::move(newSamplesHandleToUse))
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
                                          pendingOldSamples,
                                          "amplify-fade-old");
            pendingOldSamples.reset();
            applySamples(cupuacu::actions::audio::detail::materializeSampleMatrix(
                *session, pendingNewSamples, newSamplesHandle,
                "amplify-fade-new"));
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
                                          pendingNewSamples,
                                          "amplify-fade-new");
            pendingNewSamples.reset();
            applySamples(cupuacu::actions::audio::detail::materializeSampleMatrix(
                *session, pendingOldSamples, oldSamplesHandle,
                "amplify-fade-old"));
        }

        std::string getUndoDescription() override
        {
            return "Amplify/Fade";
        }

        std::string getRedoDescription() override
        {
            return "Amplify/Fade";
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
                {"kind", "amplify-fade"},
                {"settings",
                 {{"startPercent", settings.startPercent},
                  {"endPercent", settings.endPercent},
                  {"curveIndex", settings.curveIndex},
                  {"lockEnabled", settings.lockEnabled}}},
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
        static Curve clampCurve(const int curveIndex)
        {
            switch (curveIndex)
            {
            case 1:
                return Curve::Exponential;
            case 2:
                return Curve::Logarithmic;
            default:
                return Curve::Linear;
            }
        }

        static double computeCurveWeight(const Curve curveToUse,
                                         const double linearWeight)
        {
            switch (curveToUse)
            {
            case Curve::Exponential:
                return linearWeight * linearWeight;
            case Curve::Logarithmic:
                return std::log1p(linearWeight * 9.0) / std::log1p(9.0);
            case Curve::Linear:
            default:
                return linearWeight;
            }
        }

        static double gainForRelativeFrame(const AmplifyFadeSettings &settingsToUse,
                                           const Curve curveToUse,
                                           const int64_t frameIndex,
                                           const int64_t totalFrameCount)
        {
            const double startGain = settingsToUse.startPercent / 100.0;
            const double endGain = settingsToUse.endPercent / 100.0;
            if (totalFrameCount <= 1)
            {
                return startGain;
            }

            const double linearWeight =
                static_cast<double>(frameIndex) /
                static_cast<double>(totalFrameCount - 1);
            const double curvedWeight =
                computeCurveWeight(curveToUse, linearWeight);
            return startGain + (endGain - startGain) * curvedWeight;
        }

        [[nodiscard]] int getTabIndex() const
        {
            return tabIndex;
        }

        [[nodiscard]] const AmplifyFadeSettings &getSettings() const
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
        AmplifyFadeSettings settings{};
        Curve curve = Curve::Linear;
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

        double gainForFrame(const int64_t frameIndex) const
        {
            return gainForRelativeFrame(settings, curve, frameIndex, frameCount);
        }

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
                    const float oldValue =
                        document.getSample(channel, startFrame + frame);
                    oldChannel[static_cast<size_t>(frame)] = oldValue;
                    newChannel[static_cast<size_t>(frame)] =
                        static_cast<float>(oldValue * gainForFrame(frame));
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

    inline void performAmplifyFade(cupuacu::State *state,
                                   const AmplifyFadeSettings &settings)
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

        cupuacu::actions::effects::queueAmplifyFade(state, settings);
    }

    inline double computeNormalizePercent(cupuacu::State *state)
    {
        const float peak = computeTargetPeakAbsolute(state);
        if (!(peak > 0.0f))
        {
            return 100.0;
        }
        return std::clamp(100.0 / static_cast<double>(peak), 0.0, 1000.0);
    }

    class AmplifyFadePreviewProcessor : public cupuacu::audio::AudioProcessor
    {
    public:
        explicit AmplifyFadePreviewProcessor(
            const AmplifyFadeSettings &settingsToUse)
        {
            updateSettings(settingsToUse);
        }

        void updateSettings(const AmplifyFadeSettings &settingsToUse)
        {
            startPercent.store(settingsToUse.startPercent,
                               std::memory_order_release);
            endPercent.store(settingsToUse.endPercent,
                             std::memory_order_release);
            curveIndex.store(settingsToUse.curveIndex,
                             std::memory_order_release);
        }

        void process(float *interleavedStereo, const unsigned long frameCount,
                     const cupuacu::audio::AudioProcessContext &context) const override
        {
            if (!interleavedStereo || frameCount == 0 ||
                context.effectEndFrame <= context.effectStartFrame)
            {
                return;
            }

            AmplifyFadeSettings settings{};
            settings.startPercent =
                startPercent.load(std::memory_order_acquire);
            settings.endPercent = endPercent.load(std::memory_order_acquire);
            settings.curveIndex = curveIndex.load(std::memory_order_acquire);
            const auto curve =
                AmplifyFadeUndoable::clampCurve(settings.curveIndex);
            const int64_t totalFrameCount = static_cast<int64_t>(
                context.effectEndFrame - context.effectStartFrame);
            for (unsigned long i = 0; i < frameCount; ++i)
            {
                const int64_t absoluteFrame =
                    context.bufferStartFrame + static_cast<int64_t>(i);
                if (absoluteFrame < static_cast<int64_t>(context.effectStartFrame) ||
                    absoluteFrame >= static_cast<int64_t>(context.effectEndFrame))
                {
                    continue;
                }

                const int64_t relativeFrame =
                    absoluteFrame - static_cast<int64_t>(context.effectStartFrame);
                const float gain = static_cast<float>(
                    AmplifyFadeUndoable::gainForRelativeFrame(
                        settings, curve, relativeFrame, totalFrameCount));
                float *frame = interleavedStereo + i * 2;
                if (context.targetChannels != cupuacu::SelectedChannels::RIGHT)
                {
                    frame[0] *= gain;
                }
                if (context.targetChannels != cupuacu::SelectedChannels::LEFT)
                {
                    frame[1] *= gain;
                }
            }
        }

    private:
        std::atomic<double> startPercent{100.0};
        std::atomic<double> endPercent{100.0};
        std::atomic<int> curveIndex{0};
    };

    class AmplifyFadePreviewSession
        : public EffectPreviewSession<AmplifyFadeSettings>
    {
    public:
        explicit AmplifyFadePreviewSession(const AmplifyFadeSettings &settings)
            : processor(std::make_shared<AmplifyFadePreviewProcessor>(settings))
        {
        }

        std::shared_ptr<const cupuacu::audio::AudioProcessor>
        getProcessor() const override
        {
            return processor;
        }

        void updateSettings(const AmplifyFadeSettings &settings) override
        {
            processor->updateSettings(settings);
        }

    private:
        std::shared_ptr<AmplifyFadePreviewProcessor> processor;
    };

    class AmplifyFadeDialog
    {
    public:
        explicit AmplifyFadeDialog(cupuacu::State *stateToUse);

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
        double getStartPercent() const
        {
            return dialog ? dialog->getSettings().startPercent : 0.0;
        }
        double getEndPercent() const
        {
            return dialog ? dialog->getSettings().endPercent : 0.0;
        }
        int getCurveIndex() const
        {
            return dialog ? dialog->getSettings().curveIndex : 0;
        }
        bool isLocked() const
        {
            return dialog ? dialog->getSettings().lockEnabled : false;
        }

    private:
        std::unique_ptr<EffectDialogWindow<AmplifyFadeSettings>> dialog;
    };
} // namespace cupuacu::effects
