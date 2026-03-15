#pragma once

#include "../Undoable.hpp"
#include "../../gui/MainView.hpp"
#include "../../gui/Waveform.hpp"
#include "EffectUtils.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace cupuacu::actions::audio
{
    class Dynamics : public Undoable
    {
    public:
        Dynamics(State *stateToUse, const double thresholdPercentToUse,
                 const int ratioIndexToUse)
            : Undoable(stateToUse),
              thresholdPercent(std::clamp(thresholdPercentToUse, 0.0, 100.0)),
              ratioIndex(std::clamp(ratioIndexToUse, 0, 3))
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
            return "Dynamics";
        }

        std::string getRedoDescription() override
        {
            return "Dynamics";
        }

    private:
        double thresholdPercent = 100.0;
        int ratioIndex = 1;
        int64_t startFrame = 0;
        int64_t frameCount = 0;
        std::vector<int64_t> targetChannels;
        std::vector<std::vector<float>> oldSamples;
        std::vector<std::vector<float>> newSamples;

        double getRatio() const
        {
            switch (ratioIndex)
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

        float processSample(const float sample) const
        {
            const double threshold = thresholdPercent / 100.0;
            if (threshold <= 0.0)
            {
                return 0.0f;
            }

            const double magnitude = std::fabs(sample);
            if (magnitude <= threshold)
            {
                return sample;
            }

            const double ratio = getRatio();
            double compressedMagnitude = threshold;
            if (std::isfinite(ratio))
            {
                compressedMagnitude += (magnitude - threshold) / ratio;
            }
            return static_cast<float>(std::copysign(compressedMagnitude, sample));
        }

        void captureTargetsAndSamples()
        {
            if (!state)
            {
                return;
            }

            if (!getEffectTargetRange(state, startFrame, frameCount))
            {
                return;
            }

            targetChannels = getEffectTargetChannels(state);
            if (targetChannels.empty())
            {
                return;
            }

            auto &document = state->activeDocumentSession.document;
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
                        processSample(oldValue);
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
