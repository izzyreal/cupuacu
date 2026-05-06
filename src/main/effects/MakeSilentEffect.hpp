#pragma once

#include "EffectTargeting.hpp"

#include "actions/Undoable.hpp"
#include "actions/audio/SegmentStore.hpp"

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
        }

        void redo() override
        {
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

            const auto original = cupuacu::actions::audio::detail::captureOrLoadSegment(
                session, originalHandle,
                [&] { return document.captureSegment(startFrame, frameCount); });
            originalHandle = cupuacu::actions::audio::detail::storeSegmentIfNeeded(
                session, originalHandle, original, "make-silent-original");

            auto silenced = original;
            for (const int64_t channel : targetChannels)
            {
                if (channel < 0 || channel >= silenced.channelCount)
                {
                    continue;
                }
                auto &samples =
                    silenced.samples[static_cast<std::size_t>(channel)];
                std::fill(samples.begin(), samples.end(), 0.0f);
            }

            document.writeSegment(startFrame, silenced, true);
            invalidateWaveforms(session);
            restoreSelectionAndCursor(session);
        }

        void undo() override
        {
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

            const auto original = session.undoStore.readSegment(originalHandle);
            document.writeSegment(startFrame, original, true);
            invalidateWaveforms(session);
            restoreSelectionAndCursor(session);
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

    private:
        int64_t startFrame = 0;
        int64_t frameCount = 0;
        std::vector<int64_t> targetChannels;
        undo::UndoStore::SegmentHandle originalHandle;
        bool hadOldSelection = false;
        double oldSelectionStart = 0.0;
        double oldSelectionEnd = 0.0;
        int64_t oldCursorPos = 0;

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
