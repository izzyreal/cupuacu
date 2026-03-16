#pragma once

#include "EffectDialogWindow.hpp"
#include "EffectSettings.hpp"
#include "EffectTargeting.hpp"

#include "actions/Undoable.hpp"
#include "gui/MainView.hpp"
#include "gui/Waveform.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace cupuacu::effects
{
    class DynamicsUndoable : public cupuacu::actions::Undoable
    {
    public:
        DynamicsUndoable(cupuacu::State *stateToUse,
                         const DynamicsSettings &settingsToUse)
            : Undoable(stateToUse), settings(settingsToUse)
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
            return "Dynamics";
        }

        std::string getRedoDescription() override
        {
            return "Dynamics";
        }

    private:
        DynamicsSettings settings{};
        int64_t startFrame = 0;
        int64_t frameCount = 0;
        std::vector<int64_t> targetChannels;
        std::vector<std::vector<float>> oldSamples;
        std::vector<std::vector<float>> newSamples;

        double getRatio() const
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

        float processSample(const float sample) const
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

            if (!getTargetRange(state, startFrame, frameCount))
            {
                return;
            }
            targetChannels = getTargetChannels(state);
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

    inline void performDynamics(cupuacu::State *state,
                                const DynamicsSettings &settings)
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
            std::make_shared<DynamicsUndoable>(state, settings));
    }

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
