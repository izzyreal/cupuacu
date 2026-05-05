#pragma once

#include "LongTask.hpp"
#include "EffectDialogWindow.hpp"
#include "EffectSettings.hpp"
#include "EffectTargeting.hpp"

#include "actions/Undoable.hpp"
#include "actions/audio/SampleStore.hpp"
#include "audio/AudioProcessor.hpp"
#include "gui/MainViewAccess.hpp"
#include "gui/Waveform.hpp"

#include <array>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace cupuacu::actions::effects
{
    bool queueAmplifyEnvelope(
        cupuacu::State *state,
        ::cupuacu::effects::AmplifyEnvelopeSettings settings);
}

namespace cupuacu::effects
{
    inline constexpr double kAmplifyEnvelopeMinPercent = 0.0;
    inline constexpr double kAmplifyEnvelopeMaxPercent = 1000.0;
    inline constexpr double kAmplifyEnvelopeNodeSpacing = 1.0e-4;
    inline constexpr double kAmplifyEnvelopeDefaultFadeLengthMs = 100.0;
    inline constexpr std::size_t kAmplifyEnvelopePreviewTableSize = 2048;

    inline double clampAmplifyEnvelopePercent(const double value)
    {
        return std::clamp(value, kAmplifyEnvelopeMinPercent,
                          kAmplifyEnvelopeMaxPercent);
    }

    inline AmplifyEnvelopeSettings defaultAmplifyEnvelopeSettings()
    {
        return AmplifyEnvelopeSettings{};
    }

    inline double clampAmplifyEnvelopeFadeLengthMs(const double value)
    {
        return std::clamp(value, 0.0, 600000.0);
    }

    inline void
    sanitizeAmplifyEnvelopeSettings(AmplifyEnvelopeSettings &settings)
    {
        if (settings.points.size() < 2)
        {
            settings = defaultAmplifyEnvelopeSettings();
            return;
        }

        for (auto &point : settings.points)
        {
            point.position = std::clamp(point.position, 0.0, 1.0);
            point.percent = clampAmplifyEnvelopePercent(point.percent);
        }
        settings.fadeLengthMs =
            clampAmplifyEnvelopeFadeLengthMs(settings.fadeLengthMs);

        std::sort(
            settings.points.begin(), settings.points.end(),
            [](const AmplifyEnvelopePoint &lhs, const AmplifyEnvelopePoint &rhs)
            {
                return lhs.position < rhs.position;
            });

        settings.points.front().position = 0.0;
        settings.points.back().position = 1.0;
        for (std::size_t index = 1; index + 1 < settings.points.size(); ++index)
        {
            const double minPosition = settings.points[index - 1].position +
                                       kAmplifyEnvelopeNodeSpacing;
            const double maxPosition = settings.points[index + 1].position -
                                       kAmplifyEnvelopeNodeSpacing;
            settings.points[index].position = std::clamp(
                settings.points[index].position, minPosition, maxPosition);
        }
    }

    inline double
    amplifyEnvelopeGainForPosition(const AmplifyEnvelopeSettings &settingsToUse,
                                   const double position)
    {
        if (settingsToUse.points.empty())
        {
            return 1.0;
        }

        const double clampedPosition = std::clamp(position, 0.0, 1.0);
        if (clampedPosition <= settingsToUse.points.front().position)
        {
            return settingsToUse.points.front().percent / 100.0;
        }
        if (clampedPosition >= settingsToUse.points.back().position)
        {
            return settingsToUse.points.back().percent / 100.0;
        }

        for (std::size_t index = 1; index < settingsToUse.points.size();
             ++index)
        {
            const auto &left = settingsToUse.points[index - 1];
            const auto &right = settingsToUse.points[index];
            if (clampedPosition > right.position)
            {
                continue;
            }

            const double span = right.position - left.position;
            if (!(span > 0.0))
            {
                return right.percent / 100.0;
            }
            const double weight = (clampedPosition - left.position) / span;
            const double percent =
                left.percent + (right.percent - left.percent) * weight;
            return clampAmplifyEnvelopePercent(percent) / 100.0;
        }

        return settingsToUse.points.back().percent / 100.0;
    }

    inline double amplifyEnvelopeGainForRelativeFrame(
        const AmplifyEnvelopeSettings &settingsToUse, const int64_t frameIndex,
        const int64_t totalFrameCount)
    {
        if (totalFrameCount <= 1)
        {
            return settingsToUse.points.empty()
                       ? 1.0
                       : settingsToUse.points.front().percent / 100.0;
        }

        const double position = static_cast<double>(frameIndex) /
                                static_cast<double>(totalFrameCount - 1);
        return amplifyEnvelopeGainForPosition(settingsToUse, position);
    }

    inline void resetAmplifyEnvelopeSettings(AmplifyEnvelopeSettings &settings)
    {
        settings = defaultAmplifyEnvelopeSettings();
    }

    inline std::string
    formatAmplifyEnvelopeFadeLengthMs(const AmplifyEnvelopeSettings &settings)
    {
        std::ostringstream stream;
        const double rounded = std::round(settings.fadeLengthMs);
        if (std::fabs(settings.fadeLengthMs - rounded) < 1.0e-9)
        {
            stream << static_cast<long long>(std::llround(rounded));
        }
        else
        {
            stream.setf(std::ios::fixed);
            stream.precision(2);
            stream << settings.fadeLengthMs;
            auto text = stream.str();
            while (!text.empty() && text.back() == '0')
            {
                text.pop_back();
            }
            if (!text.empty() && text.back() == '.')
            {
                text.pop_back();
            }
            return text;
        }
        return stream.str();
    }

    inline bool
    parseAmplifyEnvelopeFadeLengthMs(AmplifyEnvelopeSettings &settings,
                                     const std::string &text)
    {
        try
        {
            std::size_t consumed = 0;
            const double value = std::stod(text, &consumed);
            if (consumed != text.size() || !std::isfinite(value))
            {
                return false;
            }
            settings.fadeLengthMs = clampAmplifyEnvelopeFadeLengthMs(value);
            return true;
        }
        catch (const std::exception &)
        {
            return false;
        }
    }

    inline double computeAmplifyEnvelopeNormalizePercent(cupuacu::State *state)
    {
        const float peak = computeTargetPeakAbsolute(state);
        if (!(peak > 0.0f))
        {
            return 100.0;
        }
        return std::clamp(100.0 / static_cast<double>(peak), 0.0, 1000.0);
    }

    inline void
    normalizeAmplifyEnvelopeSettings(AmplifyEnvelopeSettings &settings,
                                     cupuacu::State *state)
    {
        const double normalizePercent =
            computeAmplifyEnvelopeNormalizePercent(state);
        const double fadeLengthMs = settings.fadeLengthMs;
        const bool snapEnabled = settings.snapEnabled;
        settings.points = {{0.0, normalizePercent}, {1.0, normalizePercent}};
        settings.fadeLengthMs = fadeLengthMs;
        settings.snapEnabled = snapEnabled;
        sanitizeAmplifyEnvelopeSettings(settings);
    }

    inline void
    configureAmplifyEnvelopeFadeInOut(AmplifyEnvelopeSettings &settings,
                                      cupuacu::State *state)
    {
        const double fadeLengthMs = settings.fadeLengthMs;
        const bool snapEnabled = settings.snapEnabled;
        resetAmplifyEnvelopeSettings(settings);
        settings.fadeLengthMs = fadeLengthMs;
        settings.snapEnabled = snapEnabled;
        if (!state)
        {
            return;
        }

        int64_t startFrame = 0;
        int64_t frameCount = 0;
        if (!getTargetRange(state, startFrame, frameCount) || frameCount <= 0)
        {
            return;
        }

        const int sampleRate =
            state->getActiveDocumentSession().document.getSampleRate();
        if (sampleRate <= 0)
        {
            return;
        }

        const double fadeFrames =
            settings.fadeLengthMs * static_cast<double>(sampleRate) / 1000.0;
        const double normalizedFadeLength =
            std::clamp(fadeFrames / static_cast<double>(frameCount), 0.0, 0.5);

        if (!(normalizedFadeLength > 0.0))
        {
            return;
        }

        settings.points = {{0.0, 0.0},
                           {normalizedFadeLength, 100.0},
                           {1.0 - normalizedFadeLength, 100.0},
                           {1.0, 0.0}};
        sanitizeAmplifyEnvelopeSettings(settings);
    }

    class AmplifyEnvelopeUndoable : public cupuacu::actions::Undoable
    {
    public:
        AmplifyEnvelopeUndoable(cupuacu::State *stateToUse,
                                AmplifyEnvelopeSettings settingsToUse)
            : Undoable(stateToUse),
              tabIndex(stateToUse ? stateToUse->activeTabIndex : -1),
              settings(std::move(settingsToUse))
        {
            sanitizeAmplifyEnvelopeSettings(settings);
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

        AmplifyEnvelopeUndoable(
            cupuacu::State *stateToUse, const int tabIndexToUse,
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

        AmplifyEnvelopeUndoable(
            cupuacu::State *stateToUse, const int tabIndexToUse,
            AmplifyEnvelopeSettings settingsToUse, const int64_t startFrameToUse,
            const int64_t frameCountToUse,
            std::vector<int64_t> targetChannelsToUse,
            undo::UndoStore::SampleMatrixHandle oldSamplesHandleToUse,
            undo::UndoStore::SampleMatrixHandle newSamplesHandleToUse)
            : Undoable(stateToUse), settings(std::move(settingsToUse)),
              startFrame(startFrameToUse), frameCount(frameCountToUse),
              targetChannels(std::move(targetChannelsToUse)),
              oldSamplesHandle(std::move(oldSamplesHandleToUse)),
              newSamplesHandle(std::move(newSamplesHandleToUse)),
              tabIndex(tabIndexToUse)
        {
            sanitizeAmplifyEnvelopeSettings(settings);
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
                                          "amplify-envelope-old");
            pendingOldSamples.reset();
            applySamples(cupuacu::actions::audio::detail::materializeSampleMatrix(
                *session, pendingNewSamples, newSamplesHandle,
                "amplify-envelope-new"));
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
                                          "amplify-envelope-new");
            pendingNewSamples.reset();
            applySamples(cupuacu::actions::audio::detail::materializeSampleMatrix(
                *session, pendingOldSamples, oldSamplesHandle,
                "amplify-envelope-old"));
        }

        std::string getUndoDescription() override
        {
            return "Amplify Envelope";
        }

        std::string getRedoDescription() override
        {
            return "Amplify Envelope";
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

            nlohmann::json points = nlohmann::json::array();
            for (const auto &point : settings.points)
            {
                points.push_back({
                    {"position", point.position},
                    {"percent", point.percent},
                });
            }

            return nlohmann::json{
                {"kind", "amplify-envelope"},
                {"settings",
                 {{"points", std::move(points)},
                  {"snapEnabled", settings.snapEnabled},
                  {"fadeLengthMs", settings.fadeLengthMs}}},
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

        [[nodiscard]] const AmplifyEnvelopeSettings &getSettings() const
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

        [[nodiscard]] cupuacu::file::OverwritePreservationMutation
        overwritePreservationMutation() const override
        {
            return cupuacu::file::OverwritePreservationMutationHelper::compatible();
        }

    private:
        AmplifyEnvelopeSettings settings{};
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
            return amplifyEnvelopeGainForRelativeFrame(settings, frameIndex,
                                                       frameCount);
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
            for (std::size_t channelIndex = 0;
                 channelIndex < targetChannels.size(); ++channelIndex)
            {
                const int64_t channel = targetChannels[channelIndex];
                auto &oldChannel = oldSamples[channelIndex];
                auto &newChannel = newSamples[channelIndex];
                oldChannel.resize(static_cast<std::size_t>(frameCount));
                newChannel.resize(static_cast<std::size_t>(frameCount));
                for (int64_t frame = 0; frame < frameCount; ++frame)
                {
                    const float oldValue =
                        document.getSample(channel, startFrame + frame);
                    oldChannel[static_cast<std::size_t>(frame)] = oldValue;
                    newChannel[static_cast<std::size_t>(frame)] =
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
            for (std::size_t channelIndex = 0;
                 channelIndex < targetChannels.size(); ++channelIndex)
            {
                const int64_t channel = targetChannels[channelIndex];
                for (int64_t frame = 0; frame < frameCount; ++frame)
                {
                    document.setSample(
                        channel, startFrame + frame,
                        samples[channelIndex][static_cast<std::size_t>(frame)],
                        true);
                }
                session->getWaveformCache(channel).invalidateSamples(
                    startFrame, startFrame + frameCount - 1);
            }
            session->updateWaveformCache();
        }
    };

    inline void performAmplifyEnvelope(cupuacu::State *state,
                                       AmplifyEnvelopeSettings settings)
    {
        if (!state ||
            state->getActiveDocumentSession().document.getFrameCount() <= 0 ||
            state->getActiveDocumentSession().document.getChannelCount() <= 0)
        {
            return;
        }

        const bool hasSelection =
            state->getActiveDocumentSession().selection.isActive();
        if (hasSelection &&
            state->getActiveDocumentSession().selection.getLengthInt() <= 0)
        {
            return;
        }

        sanitizeAmplifyEnvelopeSettings(settings);
        cupuacu::actions::effects::queueAmplifyEnvelope(state,
                                                        std::move(settings));
    }

    class AmplifyEnvelopePreviewProcessor
        : public cupuacu::audio::AudioProcessor
    {
    public:
        explicit AmplifyEnvelopePreviewProcessor(
            AmplifyEnvelopeSettings settingsToUse)
        {
            sanitizeAmplifyEnvelopeSettings(settingsToUse);
            updateSettings(std::move(settingsToUse));
        }

        void updateSettings(AmplifyEnvelopeSettings settingsToUse)
        {
            sanitizeAmplifyEnvelopeSettings(settingsToUse);
            const uint8_t inactiveIndex =
                activeTableIndex.load(std::memory_order_relaxed) == 0 ? 1 : 0;
            auto &targetTable =
                inactiveIndex == 0 ? previewGainTableA : previewGainTableB;
            for (std::size_t index = 0; index < targetTable.size(); ++index)
            {
                const double position =
                    targetTable.size() <= 1
                        ? 0.0
                        : static_cast<double>(index) /
                              static_cast<double>(targetTable.size() - 1);
                targetTable[index] = static_cast<float>(
                    amplifyEnvelopeGainForPosition(settingsToUse, position));
            }
            activeTableIndex.store(inactiveIndex, std::memory_order_release);
        }

        void process(
            float *interleavedStereo, const unsigned long frameCount,
            const cupuacu::audio::AudioProcessContext &context) const override
        {
            if (!interleavedStereo || frameCount == 0 ||
                context.effectEndFrame <= context.effectStartFrame)
            {
                return;
            }

            const auto &table =
                activeTableIndex.load(std::memory_order_acquire) == 0
                    ? previewGainTableA
                    : previewGainTableB;
            const int64_t totalFrameCount = static_cast<int64_t>(
                context.effectEndFrame - context.effectStartFrame);
            for (unsigned long i = 0; i < frameCount; ++i)
            {
                const int64_t absoluteFrame =
                    context.bufferStartFrame + static_cast<int64_t>(i);
                if (absoluteFrame <
                        static_cast<int64_t>(context.effectStartFrame) ||
                    absoluteFrame >=
                        static_cast<int64_t>(context.effectEndFrame))
                {
                    continue;
                }

                const int64_t relativeFrame =
                    absoluteFrame -
                    static_cast<int64_t>(context.effectStartFrame);
                const float gain = previewGainForRelativeFrame(
                    table, relativeFrame, totalFrameCount);
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
        static float previewGainForRelativeFrame(
            const std::array<float, kAmplifyEnvelopePreviewTableSize> &table,
            const int64_t frameIndex, const int64_t totalFrameCount)
        {
            if (table.empty())
            {
                return 1.0f;
            }
            if (totalFrameCount <= 1)
            {
                return table.front();
            }

            const double position = std::clamp(
                static_cast<double>(frameIndex) /
                    static_cast<double>(totalFrameCount - 1),
                0.0, 1.0);
            const double scaledIndex =
                position * static_cast<double>(table.size() - 1);
            const auto leftIndex = static_cast<std::size_t>(
                std::clamp<int64_t>(static_cast<int64_t>(std::floor(scaledIndex)),
                                    0, static_cast<int64_t>(table.size() - 1)));
            const auto rightIndex = std::min<std::size_t>(leftIndex + 1,
                                                          table.size() - 1);
            const float leftGain = table[leftIndex];
            const float rightGain = table[rightIndex];
            const float weight = static_cast<float>(scaledIndex -
                                                    static_cast<double>(leftIndex));
            return leftGain + (rightGain - leftGain) * weight;
        }

        std::array<float, kAmplifyEnvelopePreviewTableSize> previewGainTableA{};
        std::array<float, kAmplifyEnvelopePreviewTableSize> previewGainTableB{};
        std::atomic<uint8_t> activeTableIndex{0};
    };

    class AmplifyEnvelopePreviewSession
        : public EffectPreviewSession<AmplifyEnvelopeSettings>
    {
    public:
        explicit AmplifyEnvelopePreviewSession(AmplifyEnvelopeSettings settings)
            : processor(std::make_shared<AmplifyEnvelopePreviewProcessor>(
                  std::move(settings)))
        {
        }

        std::shared_ptr<const cupuacu::audio::AudioProcessor>
        getProcessor() const override
        {
            return processor;
        }

        void updateSettings(const AmplifyEnvelopeSettings &settings) override
        {
            processor->updateSettings(settings);
        }

    private:
        std::shared_ptr<AmplifyEnvelopePreviewProcessor> processor;
    };

    class AmplifyEnvelopeDialog
    {
    public:
        explicit AmplifyEnvelopeDialog(cupuacu::State *stateToUse);

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

        const AmplifyEnvelopeSettings &getSettings() const
        {
            static const auto defaults = defaultAmplifyEnvelopeSettings();
            return dialog ? dialog->getSettings() : defaults;
        }

    private:
        std::unique_ptr<EffectDialogWindow<AmplifyEnvelopeSettings>> dialog;
    };
} // namespace cupuacu::effects
