#pragma once

#include "EffectDialogWindow.hpp"
#include "EffectSettings.hpp"
#include "EffectTargeting.hpp"

#include "actions/Undoable.hpp"
#include "gui/MainView.hpp"
#include "gui/Waveform.hpp"

#include <algorithm>
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

    private:
        AmplifyFadeSettings settings{};
        Curve curve = Curve::Linear;
        int64_t startFrame = 0;
        int64_t frameCount = 0;
        std::vector<int64_t> targetChannels;
        std::vector<std::vector<float>> oldSamples;
        std::vector<std::vector<float>> newSamples;

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

        double gainForFrame(const int64_t frameIndex) const
        {
            const double startGain = settings.startPercent / 100.0;
            const double endGain = settings.endPercent / 100.0;
            if (frameCount <= 1)
            {
                return startGain;
            }

            const double linearWeight =
                static_cast<double>(frameIndex) /
                static_cast<double>(frameCount - 1);
            const double curvedWeight = computeCurveWeight(curve, linearWeight);
            return startGain + (endGain - startGain) * curvedWeight;
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
