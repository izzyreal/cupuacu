#pragma once

#include "EffectTargeting.hpp"

#include "../LongTask.hpp"
#include "actions/Undoable.hpp"
#include "actions/audio/SegmentStore.hpp"
#include "actions/audio/TransactionalAudioEdit.hpp"

#include "gui/MainViewAccess.hpp"

#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

namespace cupuacu::effects
{
    class MakeSilentUndoable : public cupuacu::actions::Undoable
    {
    public:
        explicit MakeSilentUndoable(cupuacu::State *stateToUse)
            : Undoable(stateToUse)
        {
            captureTargetsAndState();
            installGuiUpdate();
        }

        MakeSilentUndoable(cupuacu::State *stateToUse,
                           const int64_t startFrameToUse,
                           const int64_t frameCountToUse,
                           std::vector<int64_t> targetChannelsToUse,
                           undo::UndoStore::SegmentHandle originalHandleToUse,
                           const bool hadOldSelectionToUse,
                           const double oldSelectionStartToUse,
                           const double oldSelectionEndToUse,
                           const int64_t oldCursorPosToUse)
            : Undoable(stateToUse), startFrame(startFrameToUse),
              frameCount(frameCountToUse),
              targetChannels(std::move(targetChannelsToUse)),
              originalHandle(std::move(originalHandleToUse)),
              hadOldSelection(hadOldSelectionToUse),
              oldSelectionStart(oldSelectionStartToUse),
              oldSelectionEnd(oldSelectionEndToUse),
              oldCursorPos(oldCursorPosToUse)
        {
            installGuiUpdate();
        }

        void redo() override
        {
            lastCommitted = false;
            if (!state || frameCount <= 0 || targetChannels.empty())
            {
                return;
            }

            auto &session = state->getActiveDocumentSession();
            auto &document = session.document;
            if (startFrame < 0 || startFrame + frameCount > document.getFrameCount())
            {
                return;
            }

            cupuacu::LongTaskScope longTask(
                state, "Making audio silent", "Capturing original audio", 0.0,
                false, true);
            cupuacu::renderLongTaskOverlayNow(state);
            cupuacu::actions::audio::detail::OperationProgressUi progressUi(
                state, "Capturing original audio");

            try
            {
                const auto original =
                    cupuacu::actions::audio::detail::captureOrLoadSegment(
                        session, originalHandle,
                        [&]
                        {
                            return document.captureSegment(
                                startFrame, frameCount,
                                [&](const int64_t completed,
                                    const int64_t totalToCopy)
                                {
                                    cupuacu::actions::audio::detail::
                                        publishCancelablePhaseProgress(
                                            state, progressUi,
                                            "Capturing original audio", 0.0,
                                            0.45, completed, totalToCopy);
                                });
                        });
                progressUi.publishProgress("Capturing original audio", 0.45, true);

                auto silenced = original;
                const int64_t totalChannels =
                    static_cast<int64_t>(targetChannels.size());
                for (int64_t index = 0; index < totalChannels; ++index)
                {
                    const int64_t channel = targetChannels[static_cast<std::size_t>(index)];
                    if (channel >= 0 && channel < silenced.channelCount)
                    {
                        auto &samples =
                            silenced.samples[static_cast<std::size_t>(channel)];
                        std::fill(samples.begin(), samples.end(), 0.0f);
                    }
                    cupuacu::actions::audio::detail::publishCancelablePhaseProgress(
                        state, progressUi, "Preparing silent audio", 0.45, 0.9,
                        index + 1, totalChannels);
                }
                cupuacu::throwIfLongTaskCanceled(state);

                if (originalHandle.empty())
                {
                    originalHandle =
                        cupuacu::actions::audio::detail::storeSegmentIfNeeded(
                            session, originalHandle, original,
                            "make-silent-original");
                }

                cupuacu::setLongTask(state, "Making audio silent",
                                     "Committing change", 0.9, false, false);
                progressUi.publishProgress("Committing change", 0.9, true);
                session.stopWaveformCacheBuild();
                document.writeSegment(startFrame, silenced, true);
                invalidateWaveforms(session);
                restoreSelectionAndCursor(session);
                progressUi.publishProgress("Make silent complete", 1.0, true);
                lastCommitted = true;
            }
            catch (const cupuacu::LongTaskCanceledError &)
            {
                progressUi.publishProgress("Make silent canceled", 0.0, true);
                return;
            }
        }

        void undo() override
        {
            lastCommitted = false;
            if (!state || frameCount <= 0 || originalHandle.empty())
            {
                return;
            }

            auto &session = state->getActiveDocumentSession();
            auto &document = session.document;
            if (startFrame < 0 || startFrame + frameCount > document.getFrameCount())
            {
                return;
            }

            cupuacu::LongTaskScope longTask(
                state, "Undoing make silent", "Loading original audio", 0.0,
                false, true);
            cupuacu::renderLongTaskOverlayNow(state);
            cupuacu::actions::audio::detail::OperationProgressUi progressUi(
                state, "Loading original audio");

            try
            {
                const auto original = session.undoStore.readSegment(originalHandle);
                progressUi.publishProgress("Loading original audio", 0.9, true);
                cupuacu::throwIfLongTaskCanceled(state);

                cupuacu::setLongTask(state, "Undoing make silent",
                                     "Committing undo", 0.9, false, false);
                progressUi.publishProgress("Committing undo", 0.9, true);
                session.stopWaveformCacheBuild();
                document.writeSegment(startFrame, original, true);
                invalidateWaveforms(session);
                restoreSelectionAndCursor(session);
                progressUi.publishProgress("Undo complete", 1.0, true);
                lastCommitted = true;
            }
            catch (const cupuacu::LongTaskCanceledError &)
            {
                progressUi.publishProgress("Undo canceled", 0.0, true);
                return;
            }
        }

        std::string getUndoDescription() override
        {
            return "Make silent";
        }

        std::string getRedoDescription() override
        {
            return "Make silent";
        }

        [[nodiscard]] bool canPersistForRestart() const override
        {
            return !originalHandle.empty();
        }

        [[nodiscard]] std::optional<nlohmann::json>
        serializeForRestart() const override
        {
            if (!canPersistForRestart())
            {
                return std::nullopt;
            }

            return nlohmann::json{
                {"kind", "make-silent"},
                {"startFrame", startFrame},
                {"frameCount", frameCount},
                {"targetChannels", targetChannels},
                {"originalHandle", originalHandle.path.string()},
                {"hadOldSelection", hadOldSelection},
                {"oldSelectionStart", oldSelectionStart},
                {"oldSelectionEnd", oldSelectionEnd},
                {"oldCursorPos", oldCursorPos},
            };
        }

        [[nodiscard]] cupuacu::file::OverwritePreservationMutation
        overwritePreservationMutation() const override
        {
            return cupuacu::file::OverwritePreservationMutationHelper::compatible();
        }

        [[nodiscard]] bool lastOperationCommitted() const override
        {
            return lastCommitted;
        }

    private:
        int64_t startFrame = 0;
        int64_t frameCount = 0;
        std::vector<int64_t> targetChannels;
        undo::UndoStore::SegmentHandle originalHandle;
        bool hadOldSelection = false;
        double oldSelectionStart = 0.0;
        double oldSelectionEnd = 0.0;
        int64_t oldCursorPos = 0;
        bool lastCommitted = true;

        void captureTargetsAndState()
        {
            if (!state)
            {
                return;
            }

            auto &session = state->getActiveDocumentSession();
            if (!getTargetRange(state, startFrame, frameCount) || frameCount <= 0 ||
                !session.selection.isActive())
            {
                frameCount = 0;
                return;
            }

            targetChannels = getTargetChannels(state);
            if (targetChannels.empty())
            {
                frameCount = 0;
                return;
            }

            hadOldSelection = true;
            oldSelectionStart = session.selection.getStart();
            oldSelectionEnd = session.selection.getEnd();
            oldCursorPos = session.cursor;
        }

        void installGuiUpdate()
        {
            updateGui = [state = state]() { gui::requestMainViewRefresh(state); };
        }

        void invalidateWaveforms(cupuacu::DocumentSession &session) const
        {
            if (frameCount <= 0)
            {
                return;
            }

            for (const int64_t channel : targetChannels)
            {
                if (channel < 0 ||
                    channel >= session.document.getChannelCount())
                {
                    continue;
                }
                session.getWaveformCache(channel).invalidateSamples(
                    startFrame, startFrame + frameCount - 1);
            }
            session.updateWaveformCache();
        }

        void restoreSelectionAndCursor(cupuacu::DocumentSession &session) const
        {
            session.syncSelectionAndCursorToDocumentLength();
            if (hadOldSelection)
            {
                session.selection.setValue1(oldSelectionStart);
                session.selection.setValue2(oldSelectionEnd);
            }
            else
            {
                session.selection.reset();
            }
            session.cursor = oldCursorPos;
        }
    };

    inline void performMakeSilent(cupuacu::State *state)
    {
        if (!state ||
            !state->getActiveDocumentSession().selection.isActive())
        {
            return;
        }

        state->addAndDoUndoable(std::make_shared<MakeSilentUndoable>(state));
    }
} // namespace cupuacu::effects
