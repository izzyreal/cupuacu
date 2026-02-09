#pragma once
#include "DurationMutationUndoable.hpp"
#include "../../Document.hpp"
#include <vector>
#include <cstdint>
#include <algorithm>

namespace cupuacu::actions::audio
{

    class Cut : public DurationMutationUndoable
    {
        int64_t startFrame;
        int64_t numFrames;

        std::vector<std::vector<float>> removed;

        double oldSel1 = 0.0;
        double oldSel2 = 0.0;
        int64_t oldCursorPos = 0;

    public:
        Cut(State *state, int64_t start, int64_t count)
            : DurationMutationUndoable(state), startFrame(start),
              numFrames(count)
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
            auto &doc = session.document;
            const int64_t ch = doc.getChannelCount();
            const int sr = doc.getSampleRate();
            const int64_t total = doc.getFrameCount();

            if (numFrames <= 0 || startFrame < 0 || startFrame >= total)
            {
                return;
            }

            const int64_t actualCount =
                std::min<int64_t>(numFrames, total - startFrame);
            numFrames = actualCount;

            state->clipboard.initialize(doc.getSampleFormat(), sr, ch,
                                        numFrames);

            removed.assign((size_t)ch, {});
            for (int64_t c = 0; c < ch; ++c)
            {
                removed[(size_t)c].resize((size_t)numFrames);
                for (int64_t i = 0; i < numFrames; ++i)
                {
                    float v = doc.getSample(c, startFrame + i);
                    removed[(size_t)c][(size_t)i] = v;
                    state->clipboard.setSample(c, i, v, false);
                }
            }

            doc.removeFrames(startFrame, numFrames);
            doc.updateWaveformCache();
            session.syncSelectionAndCursorToDocumentLength();

            updateCursorPos(state, startFrame);
            session.selection.reset();
        }

        void undo() override
        {
            auto &session = state->activeDocumentSession;
            auto &doc = session.document;
            const int64_t ch = doc.getChannelCount();

            doc.insertFrames(startFrame, numFrames);

            for (int64_t c = 0; c < ch; ++c)
            {
                for (int64_t i = 0; i < numFrames; ++i)
                {
                    doc.setSample(c, startFrame + i,
                                  removed[(size_t)c][(size_t)i], false);
                }
            }

            doc.updateWaveformCache();
            session.syncSelectionAndCursorToDocumentLength();

            if (oldSel1 != 0.0 || oldSel2 != 0.0)
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
            return "Cut";
        }
        std::string getRedoDescription() override
        {
            return "Cut";
        }
    };

} // namespace cupuacu::actions::audio
