#pragma once

#include "EffectDialogWindow.hpp"
#include "EffectSettings.hpp"
#include "EffectTargeting.hpp"

#include "audio/AudioProcessor.hpp"
#include "actions/Undoable.hpp"
#include "gui/MainView.hpp"
#include "gui/Waveform.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

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
            : Undoable(stateToUse), settings(settingsToUse),
              curve(clampCurve(settings.curveIndex))
        {
            captureTargetsAndSamples();
            updateGui = [this]
            {
                cupuacu::gui::Waveform::updateAllSamplePoints(state);
                cupuacu::gui::Waveform::setAllWaveformsDirty(state);
                if (state->mainView)
                {
                    state->mainView->setDirty();
                }
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
            return "Amplify/Fade";
        }

        std::string getRedoDescription() override
        {
            return "Amplify/Fade";
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

    private:
        AmplifyFadeSettings settings{};
        Curve curve = Curve::Linear;
        int64_t startFrame = 0;
        int64_t frameCount = 0;
        std::vector<int64_t> targetChannels;
        std::vector<std::vector<float>> oldSamples;
        std::vector<std::vector<float>> newSamples;

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

            auto &document = state->activeDocumentSession.document;
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
                    const float oldValue =
                        document.getSample(channel, startFrame + frame);
                    oldChannel[static_cast<size_t>(frame)] = oldValue;
                    newChannel[static_cast<size_t>(frame)] =
                        static_cast<float>(oldValue * gainForFrame(frame));
                }
            }
        }

        void applySamples(const std::vector<std::vector<float>> &samples) const
        {
            if (!state || frameCount <= 0 || targetChannels.empty())
            {
                return;
            }

            auto &document = state->activeDocumentSession.document;
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

    inline void performAmplifyFade(cupuacu::State *state,
                                   const AmplifyFadeSettings &settings)
    {
        if (!state ||
            state->activeDocumentSession.document.getFrameCount() <= 0 ||
            state->activeDocumentSession.document.getChannelCount() <= 0)
        {
            return;
        }

        const bool hasSelection = state->activeDocumentSession.selection.isActive();
        if (hasSelection &&
            state->activeDocumentSession.selection.getLengthInt() <= 0)
        {
            return;
        }

        state->addAndDoUndoable(
            std::make_shared<AmplifyFadeUndoable>(state, settings));
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
            std::atomic_store_explicit(
                &settingsSnapshot,
                std::make_shared<const AmplifyFadeSettings>(settingsToUse),
                std::memory_order_release);
        }

        void process(float *interleavedStereo, const unsigned long frameCount,
                     const cupuacu::audio::AudioProcessContext &context) const override
        {
            const auto settings =
                std::atomic_load_explicit(&settingsSnapshot, std::memory_order_acquire);
            if (!settings)
            {
                return;
            }

            if (!interleavedStereo || frameCount == 0 ||
                context.effectEndFrame <= context.effectStartFrame)
            {
                return;
            }

            const auto curve =
                AmplifyFadeUndoable::clampCurve(settings->curveIndex);
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
                        *settings, curve, relativeFrame, totalFrameCount));
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
        mutable std::shared_ptr<const AmplifyFadeSettings> settingsSnapshot;
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
