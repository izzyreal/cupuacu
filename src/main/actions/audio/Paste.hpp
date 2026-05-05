#pragma once
#include "DurationMutationUndoable.hpp"
#include "SegmentStore.hpp"
#include "../../Document.hpp"
#include <algorithm>
#include <optional>
#include <vector>
#include <cstdint>

namespace cupuacu::actions::audio
{

    class Paste : public DurationMutationUndoable
    {
        int64_t startFrame;
        int64_t endFrame;

        int64_t insertedFrameCount = 0;
        int64_t overwrittenFrameCount = 0;

        undo::UndoStore::SegmentHandle insertedHandle;
        undo::UndoStore::SegmentHandle overwrittenHandle;

        bool hadOldSelection = false;
        double oldSel1 = 0;
        double oldSel2 = 0;
        int64_t oldCursorPos = 0;

    public:
        Paste(State *state, int64_t start, int64_t end = -1)
            : DurationMutationUndoable(state), startFrame(start),
              endFrame(end)
        {
            auto &session = state->getActiveDocumentSession();
            if (session.selection.isActive())
            {
                hadOldSelection = true;
                oldSel1 = session.selection.getStart();
                oldSel2 = session.selection.getEnd();
            }
            else if (endFrame >= startFrame && endFrame >= 0)
            {
                hadOldSelection = true;
                oldSel1 = static_cast<double>(startFrame);
                oldSel2 = static_cast<double>(endFrame);
            }

            oldCursorPos = session.cursor;
        }

        Paste(State *state, const int64_t start, const int64_t end,
              const int64_t insertedFrameCountToUse,
              const int64_t overwrittenFrameCountToUse,
              undo::UndoStore::SegmentHandle insertedHandleToUse,
              undo::UndoStore::SegmentHandle overwrittenHandleToUse,
              const bool hadOldSelectionToUse, const double oldSel1ToUse,
              const double oldSel2ToUse, const int64_t oldCursorPosToUse)
            : DurationMutationUndoable(state), startFrame(start), endFrame(end),
              insertedFrameCount(insertedFrameCountToUse),
              overwrittenFrameCount(overwrittenFrameCountToUse),
              insertedHandle(std::move(insertedHandleToUse)),
              overwrittenHandle(std::move(overwrittenHandleToUse)),
              hadOldSelection(hadOldSelectionToUse), oldSel1(oldSel1ToUse),
              oldSel2(oldSel2ToUse), oldCursorPos(oldCursorPosToUse)
        {
        }

        void redo() override
        {
            auto &session = state->getActiveDocumentSession();
            const auto &clip = state->clipboard;
            if (clip.getFrameCount() == 0)
            {
                return;
            }

            auto &doc = session.document;
            const int64_t ch = doc.getChannelCount();
            const int64_t clipFrames = clip.getFrameCount();
            const int64_t docFrames = doc.getFrameCount();

            if (startFrame < 0 || startFrame > docFrames)
            {
                return;
            }

            insertedFrameCount = clipFrames;
            const auto inserted = detail::captureOrLoadSegment(
                session, insertedHandle,
                [&] { return clip.captureSegment(0, insertedFrameCount); });
            insertedHandle = detail::storeSegmentIfNeeded(
                session, insertedHandle, inserted, "paste-inserted");

            overwrittenFrameCount = 0;

            if (endFrame >= 0 && endFrame > startFrame)
            {
                overwrittenFrameCount =
                    std::min(endFrame - startFrame, docFrames - startFrame);
                overwrittenFrameCount =
                    std::max<int64_t>(0, overwrittenFrameCount);

                if (overwrittenFrameCount > 0)
                {
                    const auto overwritten = detail::captureOrLoadSegment(
                        session, overwrittenHandle,
                        [&]
                        {
                            return doc.captureSegment(startFrame,
                                                      overwrittenFrameCount);
                        });
                    overwrittenHandle = detail::storeSegmentIfNeeded(
                        session, overwrittenHandle, overwritten,
                        "paste-overwritten");

                    doc.removeFrames(startFrame, overwrittenFrameCount);
                }

                doc.insertFrames(startFrame, insertedFrameCount);
            }
            else
            {
                doc.insertFrames(startFrame, insertedFrameCount);
            }

            doc.writeSegment(startFrame, inserted, false);

            if (doc.getFrameCount() > 0)
            {
                session.invalidateWaveformSamples(startFrame,
                                                 doc.getFrameCount() - 1);
            }
            session.updateWaveformCache();
            session.syncSelectionAndCursorToDocumentLength();

            session.selection.setValue1(startFrame);
            session.selection.setValue2(startFrame + insertedFrameCount);
            updateCursorPos(state, startFrame);
        }

        void undo() override
        {
            auto &session = state->getActiveDocumentSession();
            auto &doc = session.document;
            const int64_t ch = doc.getChannelCount();
            const int64_t docFrames = doc.getFrameCount();

            if (startFrame < 0 || startFrame >= docFrames)
            {
                return;
            }

            const int64_t removeCount =
                std::min<int64_t>(insertedFrameCount, docFrames - startFrame);
            doc.removeFrames(startFrame, removeCount);

            if (endFrame >= 0 && overwrittenFrameCount > 0)
            {
                const auto overwritten =
                    session.undoStore.readSegment(overwrittenHandle);
                doc.insertFrames(startFrame, overwrittenFrameCount);
                doc.writeSegment(startFrame, overwritten, false);
            }

            if (doc.getFrameCount() > 0)
            {
                session.invalidateWaveformSamples(startFrame,
                                                 doc.getFrameCount() - 1);
            }
            session.updateWaveformCache();
            session.syncSelectionAndCursorToDocumentLength();

            if (hadOldSelection)
            {
                session.selection.setValue1(oldSel1);
                session.selection.setValue2(oldSel2);
            }
            else
            {
                session.selection.reset();
            }
            updateCursorPos(state, oldCursorPos);
        }

        std::string getUndoDescription() override
        {
            return (endFrame >= 0 ? "Paste overwrite" : "Paste insert");
        }

        std::string getRedoDescription() override
        {
            return getUndoDescription();
        }

        [[nodiscard]] bool canPersistForRestart() const override
        {
            return !insertedHandle.empty() &&
                   (overwrittenFrameCount <= 0 || !overwrittenHandle.empty());
        }

        [[nodiscard]] std::optional<nlohmann::json>
        serializeForRestart() const override
        {
            if (!canPersistForRestart())
            {
                return std::nullopt;
            }
            return nlohmann::json{
                {"kind", "paste"},
                {"startFrame", startFrame},
                {"endFrame", endFrame},
                {"insertedFrameCount", insertedFrameCount},
                {"overwrittenFrameCount", overwrittenFrameCount},
                {"insertedHandle", insertedHandle.path.string()},
                {"overwrittenHandle", overwrittenHandle.path.string()},
                {"hadOldSelection", hadOldSelection},
                {"oldSelectionStart", oldSel1},
                {"oldSelectionEnd", oldSel2},
                {"oldCursorPos", oldCursorPos},
            };
        }

        [[nodiscard]] int64_t getStartFrame() const
        {
            return startFrame;
        }

        [[nodiscard]] int64_t getEndFrame() const
        {
            return endFrame;
        }

        [[nodiscard]] int64_t getInsertedFrameCount() const
        {
            return insertedFrameCount;
        }

        [[nodiscard]] int64_t getOverwrittenFrameCount() const
        {
            return overwrittenFrameCount;
        }

        [[nodiscard]] const undo::UndoStore::SegmentHandle &getInsertedHandle() const
        {
            return insertedHandle;
        }

        [[nodiscard]] const undo::UndoStore::SegmentHandle &
        getOverwrittenHandle() const
        {
            return overwrittenHandle;
        }

        [[nodiscard]] bool getHadOldSelection() const
        {
            return hadOldSelection;
        }

        [[nodiscard]] double getOldSelectionStart() const
        {
            return oldSel1;
        }

        [[nodiscard]] double getOldSelectionEnd() const
        {
            return oldSel2;
        }

        [[nodiscard]] int64_t getOldCursorPos() const
        {
            return oldCursorPos;
        }

        [[nodiscard]] cupuacu::file::OverwritePreservationMutation
        overwritePreservationMutation() const override
        {
            return cupuacu::file::OverwritePreservationMutationHelper::compatible();
        }
    };

} // namespace cupuacu::actions::audio
