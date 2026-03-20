#pragma once

#include "EffectDialogWindow.hpp"
#include "EffectSettings.hpp"
#include "EffectTargeting.hpp"

#include "audio/AudioProcessor.hpp"
#include "actions/Undoable.hpp"
#include "gui/MainViewAccess.hpp"
#include "gui/Waveform.hpp"

#include <algorithm>
#include <atomic>
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
            return "Dynamics";
        }

        std::string getRedoDescription() override
        {
            return "Dynamics";
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

    private:
        DynamicsSettings settings{};
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
                        processSampleValue(settings, oldValue);
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

    class DynamicsPreviewProcessor : public cupuacu::audio::AudioProcessor
    {
    public:
        explicit DynamicsPreviewProcessor(const DynamicsSettings &settingsToUse)
        {
            updateSettings(settingsToUse);
        }

        void updateSettings(const DynamicsSettings &settingsToUse)
        {
            std::atomic_store_explicit(
                &settingsSnapshot,
                std::make_shared<const DynamicsSettings>(settingsToUse),
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

            if (!interleavedStereo || frameCount == 0)
            {
                return;
            }

            for (unsigned long i = 0; i < frameCount; ++i)
            {
                float *frame = interleavedStereo + i * 2;
                if (context.targetChannels != cupuacu::SelectedChannels::RIGHT)
                {
                    frame[0] =
                        DynamicsUndoable::processSampleValue(*settings, frame[0]);
                }
                if (context.targetChannels != cupuacu::SelectedChannels::LEFT)
                {
                    frame[1] =
                        DynamicsUndoable::processSampleValue(*settings, frame[1]);
                }
            }
        }

    private:
        mutable std::shared_ptr<const DynamicsSettings> settingsSnapshot;
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
