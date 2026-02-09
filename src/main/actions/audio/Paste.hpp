#pragma once
#include "DurationMutationUndoable.hpp"
#include "../../Document.hpp"
#include <algorithm>
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

        std::vector<std::vector<float>> inserted;
        std::vector<std::vector<float>> overwritten;

        double oldSel1 = 0;
        double oldSel2 = 0;
        int64_t oldCursorPos = 0;

    public:
        Paste(State *state, int64_t start, int64_t end = -1)
            : DurationMutationUndoable(state), startFrame(start),
              endFrame(end)
        {
            auto &session = state->activeDocumentSession;
            if (session.selection.isActive())
            {
                oldSel1 = session.selection.getStart();
                oldSel2 = session.selection.getEnd();
            }

            oldCursorPos = session.cursor;
        }

        void redo() override
        {
            auto &session = state->activeDocumentSession;
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
            inserted.assign((size_t)ch, {});
            for (int64_t c = 0; c < ch; ++c)
            {
                inserted[(size_t)c].resize((size_t)insertedFrameCount);
                for (int64_t i = 0; i < insertedFrameCount; ++i)
                {
                    inserted[(size_t)c][(size_t)i] = clip.getSample(c, i);
                }
            }

            overwrittenFrameCount = 0;
            overwritten.clear();

            if (endFrame >= 0 && endFrame > startFrame)
            {
                overwrittenFrameCount =
                    std::min(endFrame - startFrame, docFrames - startFrame);
                overwrittenFrameCount =
                    std::max<int64_t>(0, overwrittenFrameCount);

                overwritten.assign((size_t)ch, {});
                if (overwrittenFrameCount > 0)
                {
                    for (int64_t c = 0; c < ch; ++c)
                    {
                        overwritten[(size_t)c].resize(
                            (size_t)overwrittenFrameCount);
                        for (int64_t i = 0; i < overwrittenFrameCount; ++i)
                        {
                            overwritten[(size_t)c][(size_t)i] =
                                doc.getSample(c, startFrame + i);
                        }
                    }

                    doc.removeFrames(startFrame, overwrittenFrameCount);
                }

                doc.insertFrames(startFrame, insertedFrameCount);
            }
            else
            {
                doc.insertFrames(startFrame, insertedFrameCount);
            }

            const int64_t maxWritable = std::min<int64_t>(
                insertedFrameCount, doc.getFrameCount() - startFrame);
            for (int64_t c = 0; c < ch; ++c)
            {
                for (int64_t i = 0; i < maxWritable; ++i)
                {
                    doc.setSample(c, startFrame + i,
                                  inserted[(size_t)c][(size_t)i], false);
                }
            }

            doc.updateWaveformCache();
            session.syncSelectionAndCursorToDocumentLength();

            session.selection.setValue1(startFrame);
            session.selection.setValue2(startFrame + insertedFrameCount);
            updateCursorPos(state, startFrame);
        }

        void undo() override
        {
            auto &session = state->activeDocumentSession;
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
                doc.insertFrames(startFrame, overwrittenFrameCount);
                const int64_t maxRestore = std::min<int64_t>(
                    overwrittenFrameCount, doc.getFrameCount() - startFrame);

                for (int64_t c = 0; c < ch; ++c)
                {
                    for (int64_t i = 0; i < maxRestore; ++i)
                    {
                        doc.setSample(c, startFrame + i,
                                      overwritten[(size_t)c][(size_t)i], false);
                    }
                }
            }

            doc.updateWaveformCache();
            session.syncSelectionAndCursorToDocumentLength();

            if (oldSel1 != 0 && oldSel2 != 0)
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
    };

} // namespace cupuacu::actions::audio
