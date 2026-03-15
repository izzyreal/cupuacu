#pragma once

#include "../Undoable.hpp"
#include "../../SelectedChannels.hpp"
#include "../../gui/MainView.hpp"
#include "../../gui/Waveform.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace cupuacu::actions::audio
{
    class AmplifyFade : public Undoable
    {
    public:
        enum class Curve
        {
            Linear,
            Exponential,
            Logarithmic
        };

        AmplifyFade(State *stateToUse, const double startPercentToUse,
                    const double endPercentToUse, const int curveIndexToUse)
            : Undoable(stateToUse), startPercent(startPercentToUse),
              endPercent(endPercentToUse),
              curve(clampCurve(curveIndexToUse))
        {
            captureTargetsAndSamples();
            updateGui = [this]
            {
                gui::Waveform::updateAllSamplePoints(state);
                gui::Waveform::setAllWaveformsDirty(state);
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
        double startPercent = 100.0;
        double endPercent = 100.0;
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
            const double startGain = startPercent / 100.0;
            const double endGain = endPercent / 100.0;
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

            auto &session = state->activeDocumentSession;
            auto &document = session.document;
            const int64_t channelCount = document.getChannelCount();
            if (channelCount <= 0)
            {
                return;
            }

            if (session.selection.isActive())
            {
                startFrame = session.selection.getStartInt();
                frameCount = session.selection.getLengthInt();
            }
            else
            {
                startFrame = 0;
                frameCount = document.getFrameCount();
            }

            if (frameCount <= 0)
            {
                return;
            }

            if (!session.selection.isActive())
            {
                for (int64_t channel = 0; channel < channelCount; ++channel)
                {
                    targetChannels.push_back(channel);
                }
            }
            else
            {
                SelectedChannels selectedChannels = SelectedChannels::BOTH;
                if (state->mainDocumentSessionWindow)
                {
                    selectedChannels =
                        state->mainDocumentSessionWindow->getViewState()
                            .selectedChannels;
                }

                if (channelCount <= 1 ||
                    selectedChannels == SelectedChannels::BOTH)
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
} // namespace cupuacu::actions::audio
