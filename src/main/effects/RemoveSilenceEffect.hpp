#pragma once

#include "EffectDialogWindow.hpp"
#include "EffectSettings.hpp"
#include "EffectTargeting.hpp"
#include "LongTask.hpp"

#include "actions/audio/DurationMutationUndoable.hpp"
#include "actions/audio/SampleStore.hpp"
#include "file/SampleQuantization.hpp"
#include "gui/DropdownMenu.hpp"
#include "gui/Label.hpp"
#include "gui/MainViewAccess.hpp"
#include "gui/OpaqueRect.hpp"
#include "gui/TextButton.hpp"
#include "gui/TextInput.hpp"
#include "gui/Window.hpp"
#include "gui/Waveform.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace cupuacu::actions::effects
{
    bool queueRemoveSilence(
        cupuacu::State *state,
        const ::cupuacu::effects::RemoveSilenceSettings &settings);
}

namespace cupuacu::effects
{
    enum class RemoveSilenceMode
    {
        FromBeginningAndEnd = 0,
        AllSilencesInSection = 1
    };

    enum class RemoveSilenceThresholdUnit
    {
        Db = 0,
        SampleValue = 1
    };

    struct SilenceRange
    {
        int64_t startFrame = 0;
        int64_t frameCount = 0;
    };

    inline RemoveSilenceMode removeSilenceModeFromIndex(const int index)
    {
        return index == 1 ? RemoveSilenceMode::AllSilencesInSection
                          : RemoveSilenceMode::FromBeginningAndEnd;
    }

    inline RemoveSilenceThresholdUnit removeSilenceThresholdUnitFromIndex(
        const int index)
    {
        return index == 1 ? RemoveSilenceThresholdUnit::SampleValue
                          : RemoveSilenceThresholdUnit::Db;
    }

    inline std::vector<std::string> removeSilenceModeLabels()
    {
        return {"From beginning and end", "All silences in section"};
    }

    inline std::vector<std::string> removeSilenceThresholdUnitLabels()
    {
        return {"dB", "Sample Value"};
    }

    inline const std::vector<double> &removeSilenceMinimumLengthOptionsMs()
    {
        static const std::vector<double> options{1.0, 2.0, 5.0, 10.0, 20.0, 50.0,
                                                 100.0};
        return options;
    }

    inline std::vector<std::string> removeSilenceMinimumLengthLabels()
    {
        std::vector<std::string> labels;
        for (const double value : removeSilenceMinimumLengthOptionsMs())
        {
            std::ostringstream stream;
            stream << std::fixed << std::setprecision(value < 10.0 ? 1 : 0)
                   << value << " ms";
            labels.push_back(stream.str());
        }
        return labels;
    }

    inline double normalizeRemoveSilenceMinimumLengthMs(const double value)
    {
        const auto &options = removeSilenceMinimumLengthOptionsMs();
        auto best = options.front();
        auto bestDistance = std::fabs(best - value);
        for (const double option : options)
        {
            const double distance = std::fabs(option - value);
            if (distance < bestDistance)
            {
                best = option;
                bestDistance = distance;
            }
        }
        return best;
    }

    inline int removeSilenceMinimumLengthIndex(const double value)
    {
        const double normalized = normalizeRemoveSilenceMinimumLengthMs(value);
        const auto &options = removeSilenceMinimumLengthOptionsMs();
        for (std::size_t i = 0; i < options.size(); ++i)
        {
            if (std::fabs(options[i] - normalized) < 1.0e-9)
            {
                return static_cast<int>(i);
            }
        }
        return 0;
    }

    inline int64_t relevantSectionStart(cupuacu::State *state)
    {
        int64_t start = 0;
        int64_t frameCount = 0;
        return getTargetRange(state, start, frameCount) ? start : 0;
    }

    inline int64_t relevantSectionLength(cupuacu::State *state)
    {
        int64_t start = 0;
        int64_t frameCount = 0;
        (void)start;
        return getTargetRange(state, start, frameCount) ? frameCount : 0;
    }

    inline double sampleValueDisplayMax(const cupuacu::SampleFormat format)
    {
        if (file::isIntegerPcmSampleFormat(format))
        {
            const int bitDepth = file::pcmBitDepth(format);
            if (bitDepth <= 0 || bitDepth >= 63)
            {
                return 0.0;
            }
            return static_cast<double>((int64_t{1} << (bitDepth - 1)) - 1);
        }
        return 1.0;
    }

    inline double minimumThresholdStep(const cupuacu::SampleFormat format)
    {
        const double maxValue = sampleValueDisplayMax(format);
        if (maxValue > 1.0)
        {
            return 1.0 / maxValue;
        }
        return 1.0e-5;
    }

    inline double thresholdAbsoluteFromDb(const double db)
    {
        if (!std::isfinite(db))
        {
            return 0.0;
        }
        return std::clamp(std::pow(10.0, db / 20.0), 0.0, 1.0);
    }

    inline double thresholdDbFromAbsolute(const double absolute)
    {
        if (!(absolute > 0.0))
        {
            return -120.0;
        }
        return 20.0 * std::log10(std::clamp(absolute, 1.0e-12, 1.0));
    }

    inline double thresholdAbsoluteFromSampleValue(
        const cupuacu::SampleFormat format, const double sampleValue)
    {
        const double maxValue = sampleValueDisplayMax(format);
        if (!(maxValue > 0.0))
        {
            return 0.0;
        }

        const double absoluteValue = std::fabs(sampleValue);
        if (maxValue > 1.0)
        {
            return std::clamp(absoluteValue / maxValue, 0.0, 1.0);
        }
        return std::clamp(absoluteValue, 0.0, 1.0);
    }

    inline double thresholdSampleValueFromAbsolute(
        const cupuacu::SampleFormat format, const double absolute)
    {
        const double clamped = std::clamp(absolute, 0.0, 1.0);
        const double maxValue = sampleValueDisplayMax(format);
        return maxValue > 1.0 ? std::round(clamped * maxValue) : clamped;
    }

    inline double currentThresholdAbsolute(cupuacu::State *state,
                                           const RemoveSilenceSettings &settings)
    {
        if (!state)
        {
            return 0.0;
        }

        const auto format = state->getActiveDocumentSession().document.getSampleFormat();
        if (removeSilenceThresholdUnitFromIndex(settings.thresholdUnitIndex) ==
            RemoveSilenceThresholdUnit::Db)
        {
            return thresholdAbsoluteFromDb(settings.thresholdDb);
        }

        return thresholdAbsoluteFromSampleValue(format,
                                                settings.thresholdSampleValue);
    }

    inline double measureFrameMagnitude(const cupuacu::Document &document,
                                        const std::vector<int64_t> &channels,
                                        const int64_t frame)
    {
        double magnitude = 0.0;
        for (const int64_t channel : channels)
        {
            magnitude = std::max(
                magnitude,
                static_cast<double>(
                    std::fabs(document.getSample(channel, frame))));
        }
        return magnitude;
    }

    inline int64_t minimumSilenceFrames(const cupuacu::Document &document,
                                        const RemoveSilenceSettings &settings)
    {
        const int sampleRate = document.getSampleRate();
        if (sampleRate <= 0)
        {
            return 1;
        }

        const double clampedMs =
            std::clamp(settings.minimumSilenceLengthMs, 0.0, 5000.0);
        const double frames = clampedMs * static_cast<double>(sampleRate) / 1000.0;
        return std::max<int64_t>(1, static_cast<int64_t>(std::ceil(frames)));
    }

    inline std::vector<SilenceRange> detectSilentRanges(
        const cupuacu::Document &document, const std::vector<int64_t> &channels,
        const int64_t startFrame, const int64_t frameCount,
        const double thresholdAbsolute, const RemoveSilenceSettings &settings)
    {
        std::vector<SilenceRange> runs;
        if (frameCount <= 0 || channels.empty())
        {
            return runs;
        }

        bool inRun = false;
        int64_t runStart = startFrame;
        const int64_t endFrame = startFrame + frameCount;
        const int64_t minFrames = minimumSilenceFrames(document, settings);
        for (int64_t frame = startFrame; frame < endFrame; ++frame)
        {
            const bool isSilent =
                measureFrameMagnitude(document, channels, frame) <= thresholdAbsolute;
            if (isSilent && !inRun)
            {
                inRun = true;
                runStart = frame;
            }
            else if (!isSilent && inRun)
            {
                const int64_t runLength = frame - runStart;
                if (runLength >= minFrames)
                {
                    runs.push_back(
                        {.startFrame = runStart, .frameCount = runLength});
                }
                inRun = false;
            }
        }

        if (inRun)
        {
            const int64_t runLength = endFrame - runStart;
            if (runLength >= minFrames)
            {
                runs.push_back(
                    {.startFrame = runStart, .frameCount = runLength});
            }
        }

        return runs;
    }

    inline std::vector<SilenceRange> planSilenceRemoval(
        const cupuacu::Document &document, const std::vector<int64_t> &channels,
        const int64_t startFrame, const int64_t frameCount,
        const double thresholdAbsolute, const RemoveSilenceMode mode,
        const RemoveSilenceSettings &settings)
    {
        auto runs = detectSilentRanges(document, channels, startFrame, frameCount,
                                       thresholdAbsolute, settings);
        if (mode == RemoveSilenceMode::AllSilencesInSection || runs.empty())
        {
            return runs;
        }

        std::vector<SilenceRange> trimmedRuns;
        const int64_t endFrame = startFrame + frameCount;
        if (!runs.empty() && runs.front().startFrame == startFrame)
        {
            trimmedRuns.push_back(runs.front());
        }
        if (runs.size() > 1 && runs.back().startFrame + runs.back().frameCount ==
                                   endFrame)
        {
            trimmedRuns.push_back(runs.back());
        }
        else if (runs.size() == 1 &&
                 runs.front().startFrame + runs.front().frameCount == endFrame &&
                 trimmedRuns.empty())
        {
            trimmedRuns.push_back(runs.front());
        }
        return trimmedRuns;
    }

    inline double computeAutoSilenceThresholdAbsolute(
        const cupuacu::Document &document, const std::vector<int64_t> &channels,
        const int64_t startFrame, const int64_t frameCount)
    {
        if (frameCount <= 0 || channels.empty())
        {
            return minimumThresholdStep(document.getSampleFormat());
        }

        const int64_t blockSize = std::clamp<int64_t>(frameCount / 256, 64, 4096);
        std::vector<double> blockPeaks;
        for (int64_t blockStart = startFrame; blockStart < startFrame + frameCount;
             blockStart += blockSize)
        {
            const int64_t blockEnd =
                std::min(startFrame + frameCount, blockStart + blockSize);
            double peak = 0.0;
            for (int64_t frame = blockStart; frame < blockEnd; ++frame)
            {
                peak = std::max(peak, measureFrameMagnitude(document, channels, frame));
            }
            blockPeaks.push_back(peak);
        }

        if (blockPeaks.empty())
        {
            return minimumThresholdStep(document.getSampleFormat());
        }

        std::sort(blockPeaks.begin(), blockPeaks.end());
        const double baseline =
            blockPeaks[std::min<std::size_t>(blockPeaks.size() - 1,
                                             blockPeaks.size() / 10)];
        const double minStep = minimumThresholdStep(document.getSampleFormat());
        return std::clamp(std::max(minStep, baseline * 2.0), minStep, 0.1);
    }

    class RemoveSilenceUndoable : public cupuacu::actions::audio::DurationMutationUndoable
    {
    public:
        RemoveSilenceUndoable(cupuacu::State *stateToUse,
                              std::vector<SilenceRange> runsToRemove,
                              const int64_t relevantStartToUse,
                              const int64_t relevantLengthToUse)
            : DurationMutationUndoable(stateToUse),
              tabIndex(stateToUse ? stateToUse->activeTabIndex : -1),
              runs(std::move(runsToRemove)), relevantStart(relevantStartToUse),
              originalRelevantLength(relevantLengthToUse)
        {
            updateGui = [this]
            {
                if (!state || state->activeTabIndex != tabIndex)
                {
                    return;
                }
                afterDurationMutationUi();
                cupuacu::actions::applyDurationChangeViewPolicy(state);
            };
            captureRemovedSamples();
        }

        RemoveSilenceUndoable(
            cupuacu::State *stateToUse, const int tabIndexToUse,
            std::vector<SilenceRange> runsToRemove,
            const int64_t relevantStartToUse,
            const int64_t relevantLengthToUse,
            std::vector<std::vector<std::vector<float>>> removedSamplesToUse,
            const int64_t originalCursorToUse, const bool hadSelectionToUse)
            : DurationMutationUndoable(stateToUse),
              tabIndex(tabIndexToUse),
              runs(std::move(runsToRemove)),
              pendingRemovedSamples(std::move(removedSamplesToUse)),
              relevantStart(relevantStartToUse),
              originalRelevantLength(relevantLengthToUse),
              originalCursor(originalCursorToUse),
              hadSelection(hadSelectionToUse)
        {
            updateGui = [this]
            {
                if (!state || state->activeTabIndex != tabIndex)
                {
                    return;
                }
                afterDurationMutationUi();
                cupuacu::actions::applyDurationChangeViewPolicy(state);
            };
            for (const auto &run : runs)
            {
                removedFrameCount += run.frameCount;
            }
        }

        RemoveSilenceUndoable(
            cupuacu::State *stateToUse, const int tabIndexToUse,
            std::vector<SilenceRange> runsToRemove,
            const int64_t relevantStartToUse,
            const int64_t relevantLengthToUse,
            undo::UndoStore::SampleCubeHandle removedSamplesHandleToUse,
            const int64_t originalCursorToUse, const bool hadSelectionToUse)
            : DurationMutationUndoable(stateToUse), tabIndex(tabIndexToUse),
              runs(std::move(runsToRemove)),
              removedSamplesHandle(std::move(removedSamplesHandleToUse)),
              relevantStart(relevantStartToUse),
              originalRelevantLength(relevantLengthToUse),
              originalCursor(originalCursorToUse),
              hadSelection(hadSelectionToUse)
        {
            updateGui = [this]
            {
                if (!state || state->activeTabIndex != tabIndex)
                {
                    return;
                }
                afterDurationMutationUi();
                cupuacu::actions::applyDurationChangeViewPolicy(state);
            };
            for (const auto &run : runs)
            {
                removedFrameCount += run.frameCount;
            }
        }

        void redo() override
        {
            if (!state || runs.empty())
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
            removedSamplesHandle = cupuacu::actions::audio::detail::
                storeSampleCubeIfNeeded(session, removedSamplesHandle,
                                        pendingRemovedSamples,
                                        "remove-silence-removed");
            pendingRemovedSamples.reset();
            for (auto it = runs.rbegin(); it != runs.rend(); ++it)
            {
                document.removeFrames(it->startFrame, it->frameCount);
            }
            if (document.getFrameCount() > 0)
            {
                session.invalidateWaveformSamples(0,
                                                  document.getFrameCount() - 1);
            }
            session.updateWaveformCache();
            session.syncSelectionAndCursorToDocumentLength();
            const int64_t newRelevantLength =
                std::max<int64_t>(0, originalRelevantLength - removedFrameCount);
            session.cursor = relevantStart;
            if (hadSelection)
            {
                session.selection.setValue1(relevantStart);
                session.selection.setValue2(relevantStart + newRelevantLength);
            }
            else
            {
                session.selection.reset();
            }
        }

        void undo() override
        {
            if (!state || runs.empty())
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
            const auto removedSamples =
                cupuacu::actions::audio::detail::materializeSampleCube(
                    session, pendingRemovedSamples, removedSamplesHandle,
                    "remove-silence-removed");
            const int64_t channelCount = document.getChannelCount();
            for (std::size_t runIndex = 0; runIndex < runs.size(); ++runIndex)
            {
                const auto &run = runs[runIndex];
                document.insertFrames(run.startFrame, run.frameCount);
                for (int64_t channel = 0; channel < channelCount; ++channel)
                {
                    for (int64_t frame = 0; frame < run.frameCount; ++frame)
                    {
                        document.setSample(
                            channel, run.startFrame + frame,
                            removedSamples[runIndex][channel][frame], false);
                    }
                }
            }
            if (document.getFrameCount() > 0)
            {
                session.invalidateWaveformSamples(0,
                                                  document.getFrameCount() - 1);
            }
            session.updateWaveformCache();
            session.syncSelectionAndCursorToDocumentLength();
            session.cursor = originalCursor;
            if (hadSelection)
            {
                session.selection.setValue1(relevantStart);
                session.selection.setValue2(relevantStart + originalRelevantLength);
            }
            else
            {
                session.selection.reset();
            }
        }

        std::string getUndoDescription() override
        {
            return "Remove silence";
        }

        std::string getRedoDescription() override
        {
            return "Remove silence";
        }

        [[nodiscard]] bool canPersistForRestart() const override
        {
            return !removedSamplesHandle.empty();
        }

        [[nodiscard]] std::optional<nlohmann::json>
        serializeForRestart() const override
        {
            if (!canPersistForRestart())
            {
                return std::nullopt;
            }

            nlohmann::json runsJson = nlohmann::json::array();
            for (const auto &run : runs)
            {
                runsJson.push_back({
                    {"startFrame", run.startFrame},
                    {"frameCount", run.frameCount},
                });
            }

            return nlohmann::json{
                {"kind", "remove-silence-duration"},
                {"runs", std::move(runsJson)},
                {"relevantStart", relevantStart},
                {"originalRelevantLength", originalRelevantLength},
                {"removedSamplesHandle", removedSamplesHandle.path.string()},
                {"originalCursor", originalCursor},
                {"hadSelection", hadSelection},
            };
        }

        [[nodiscard]] int getTabIndex() const
        {
            return tabIndex;
        }

        [[nodiscard]] const std::vector<SilenceRange> &getRuns() const
        {
            return runs;
        }

        [[nodiscard]] const undo::UndoStore::SampleCubeHandle &
        getRemovedSamplesHandle() const
        {
            return removedSamplesHandle;
        }

        [[nodiscard]] int64_t getRelevantStart() const
        {
            return relevantStart;
        }

        [[nodiscard]] int64_t getOriginalRelevantLength() const
        {
            return originalRelevantLength;
        }

        [[nodiscard]] int64_t getOriginalCursor() const
        {
            return originalCursor;
        }

        [[nodiscard]] bool getHadSelection() const
        {
            return hadSelection;
        }

        [[nodiscard]] cupuacu::file::OverwritePreservationMutation
        overwritePreservationMutation() const override
        {
            return cupuacu::file::OverwritePreservationMutationHelper::compatible();
        }

    protected:
        void afterDurationMutationUi() override
        {
            cupuacu::gui::Waveform::updateAllSamplePoints(state);
            cupuacu::gui::Waveform::setAllWaveformsDirty(state);
            cupuacu::gui::requestMainViewRefresh(state);
        }

    private:
        int tabIndex = -1;
        std::vector<SilenceRange> runs;
        std::optional<cupuacu::actions::audio::detail::SampleCube>
            pendingRemovedSamples;
        undo::UndoStore::SampleCubeHandle removedSamplesHandle;
        int64_t relevantStart = 0;
        int64_t originalRelevantLength = 0;
        int64_t removedFrameCount = 0;
        int64_t originalCursor = 0;
        bool hadSelection = false;

        void captureRemovedSamples()
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
            const auto &document = session.document;
            const int64_t channelCount = document.getChannelCount();
            originalCursor = session.cursor;
            hadSelection = session.selection.isActive();
            cupuacu::actions::audio::detail::SampleCube removedSamples;
            removedSamples.resize(runs.size());
            for (std::size_t runIndex = 0; runIndex < runs.size(); ++runIndex)
            {
                const auto &run = runs[runIndex];
                removedFrameCount += run.frameCount;
                removedSamples[runIndex].resize(static_cast<std::size_t>(channelCount));
                for (int64_t channel = 0; channel < channelCount; ++channel)
                {
                    auto &channelSamples = removedSamples[runIndex][channel];
                    channelSamples.resize(static_cast<std::size_t>(run.frameCount));
                    for (int64_t frame = 0; frame < run.frameCount; ++frame)
                    {
                        channelSamples[static_cast<std::size_t>(frame)] =
                            document.getSample(channel, run.startFrame + frame);
                    }
                }
            }
            pendingRemovedSamples = std::move(removedSamples);
        }
    };

    class RemoveSilenceChannelCompactUndoable : public cupuacu::actions::Undoable
    {
    public:
        RemoveSilenceChannelCompactUndoable(cupuacu::State *stateToUse,
                                            std::vector<int64_t> targetChannelsToUse,
                                            std::vector<SilenceRange> runsToRemove,
                                            const int64_t startFrameToUse,
                                            const int64_t frameCountToUse)
            : Undoable(stateToUse),
              tabIndex(stateToUse ? stateToUse->activeTabIndex : -1),
              targetChannels(std::move(targetChannelsToUse)),
              runs(std::move(runsToRemove)), startFrame(startFrameToUse),
              frameCount(frameCountToUse)
        {
            captureSamples();
            updateGui = [this]
            {
                cupuacu::gui::Waveform::updateAllSamplePoints(state);
                cupuacu::gui::Waveform::setAllWaveformsDirty(state);
                cupuacu::gui::requestMainViewRefresh(state);
            };
        }

        RemoveSilenceChannelCompactUndoable(
            cupuacu::State *stateToUse, const int tabIndexToUse,
            std::vector<int64_t> targetChannelsToUse,
            std::vector<SilenceRange> runsToRemove,
            const int64_t startFrameToUse, const int64_t frameCountToUse,
            std::vector<std::vector<float>> oldSamplesToUse,
            std::vector<std::vector<float>> newSamplesToUse)
            : Undoable(stateToUse),
              tabIndex(tabIndexToUse),
              targetChannels(std::move(targetChannelsToUse)),
              runs(std::move(runsToRemove)),
              startFrame(startFrameToUse),
              frameCount(frameCountToUse),
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

        RemoveSilenceChannelCompactUndoable(
            cupuacu::State *stateToUse, const int tabIndexToUse,
            std::vector<int64_t> targetChannelsToUse,
            std::vector<SilenceRange> runsToRemove,
            const int64_t startFrameToUse, const int64_t frameCountToUse,
            undo::UndoStore::SampleMatrixHandle oldSamplesHandleToUse,
            undo::UndoStore::SampleMatrixHandle newSamplesHandleToUse)
            : Undoable(stateToUse), tabIndex(tabIndexToUse),
              targetChannels(std::move(targetChannelsToUse)),
              runs(std::move(runsToRemove)), startFrame(startFrameToUse),
              frameCount(frameCountToUse),
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
                                          "remove-silence-compact-old");
            pendingOldSamples.reset();
            applySamples(cupuacu::actions::audio::detail::materializeSampleMatrix(
                *session, pendingNewSamples, newSamplesHandle,
                "remove-silence-compact-new"));
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
                                          "remove-silence-compact-new");
            pendingNewSamples.reset();
            applySamples(cupuacu::actions::audio::detail::materializeSampleMatrix(
                *session, pendingOldSamples, oldSamplesHandle,
                "remove-silence-compact-old"));
        }

        std::string getUndoDescription() override
        {
            return "Remove silence";
        }

        std::string getRedoDescription() override
        {
            return "Remove silence";
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

            nlohmann::json runsJson = nlohmann::json::array();
            for (const auto &run : runs)
            {
                runsJson.push_back({
                    {"startFrame", run.startFrame},
                    {"frameCount", run.frameCount},
                });
            }

            return nlohmann::json{
                {"kind", "remove-silence-compact"},
                {"targetChannels", targetChannels},
                {"runs", std::move(runsJson)},
                {"startFrame", startFrame},
                {"frameCount", frameCount},
                {"oldSamplesHandle", oldSamplesHandle.path.string()},
                {"newSamplesHandle", newSamplesHandle.path.string()},
            };
        }

        [[nodiscard]] int getTabIndex() const
        {
            return tabIndex;
        }

        [[nodiscard]] const std::vector<int64_t> &getTargetChannels() const
        {
            return targetChannels;
        }

        [[nodiscard]] const std::vector<SilenceRange> &getRuns() const
        {
            return runs;
        }

        [[nodiscard]] int64_t getStartFrame() const
        {
            return startFrame;
        }

        [[nodiscard]] int64_t getFrameCount() const
        {
            return frameCount;
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
        int tabIndex = -1;
        std::vector<int64_t> targetChannels;
        std::vector<SilenceRange> runs;
        int64_t startFrame = 0;
        int64_t frameCount = 0;
        std::optional<cupuacu::actions::audio::detail::SampleMatrix>
            pendingOldSamples;
        std::optional<cupuacu::actions::audio::detail::SampleMatrix>
            pendingNewSamples;
        undo::UndoStore::SampleMatrixHandle oldSamplesHandle;
        undo::UndoStore::SampleMatrixHandle newSamplesHandle;

        void captureSamples()
        {
            if (!state || frameCount <= 0 || targetChannels.empty())
            {
                return;
            }

            if (tabIndex < 0 || tabIndex >= static_cast<int>(state->tabs.size()))
            {
                return;
            }

            const auto &document =
                state->tabs[static_cast<std::size_t>(tabIndex)].session.document;
            cupuacu::actions::audio::detail::SampleMatrix oldSamples;
            cupuacu::actions::audio::detail::SampleMatrix newSamples;
            oldSamples.resize(targetChannels.size());
            newSamples.resize(targetChannels.size());

            for (std::size_t channelIndex = 0; channelIndex < targetChannels.size();
                 ++channelIndex)
            {
                const auto channel = targetChannels[channelIndex];
                auto &oldChannel = oldSamples[channelIndex];
                auto &newChannel = newSamples[channelIndex];
                oldChannel.resize(static_cast<std::size_t>(frameCount));
                newChannel.assign(static_cast<std::size_t>(frameCount), 0.0f);

                for (int64_t frame = 0; frame < frameCount; ++frame)
                {
                    oldChannel[static_cast<std::size_t>(frame)] =
                        document.getSample(channel, startFrame + frame);
                }

                int64_t writeFrame = 0;
                std::size_t runIndex = 0;
                for (int64_t frame = 0; frame < frameCount; ++frame)
                {
                    const int64_t absoluteFrame = startFrame + frame;
                    while (runIndex < runs.size() &&
                           absoluteFrame >= runs[runIndex].startFrame +
                                                runs[runIndex].frameCount)
                    {
                        ++runIndex;
                    }

                    const bool insideRun =
                        runIndex < runs.size() &&
                        absoluteFrame >= runs[runIndex].startFrame &&
                        absoluteFrame <
                            runs[runIndex].startFrame + runs[runIndex].frameCount;
                    if (insideRun)
                    {
                        continue;
                    }

                    if (writeFrame < frameCount)
                    {
                        newChannel[static_cast<std::size_t>(writeFrame)] =
                            oldChannel[static_cast<std::size_t>(frame)];
                        ++writeFrame;
                    }
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
            for (std::size_t channelIndex = 0; channelIndex < targetChannels.size();
                 ++channelIndex)
            {
                const int64_t channel = targetChannels[channelIndex];
                for (int64_t frame = 0; frame < frameCount; ++frame)
                {
                    document.setSample(
                        channel, startFrame + frame,
                        samples[channelIndex][static_cast<std::size_t>(frame)], true);
                }
                session->getWaveformCache(channel).invalidateSamples(
                    startFrame, startFrame + frameCount - 1);
            }
            session->updateWaveformCache();
        }
    };

    inline void performRemoveSilence(cupuacu::State *state,
                                     const RemoveSilenceSettings &settings)
    {
        if (!state)
        {
            return;
        }

        int64_t startFrame = 0;
        int64_t frameCount = 0;
        if (!getTargetRange(state, startFrame, frameCount))
        {
            return;
        }

        auto &document = state->getActiveDocumentSession().document;
        const auto targetChannels = getTargetChannels(state);
        if (targetChannels.empty())
        {
            return;
        }

        cupuacu::actions::effects::queueRemoveSilence(state, settings);
    }

    class RemoveSilenceDialog
    {
    public:
        explicit RemoveSilenceDialog(cupuacu::State *stateToUse);

        bool isOpen() const;
        void raise() const;
        cupuacu::gui::Window *getWindow() const;

    private:
        std::unique_ptr<EffectDialogWindow<RemoveSilenceSettings>> dialog;
    };
} // namespace cupuacu::effects
