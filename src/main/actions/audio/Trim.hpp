#pragma once
#include "../Undoable.hpp"
#include "../../Document.hpp"
#include "../../gui/MainView.hpp"
#include "../Zoom.hpp"
#include <vector>
#include <cstdint>
#include <algorithm>

namespace cupuacu::actions::audio
{

    class Trim : public Undoable
    {
        int64_t startFrame;
        int64_t length;

        int sampleRate = 0;
        SampleFormat format = SampleFormat::Unknown;

        int64_t oldTotal = 0;
        int64_t beforeCount = 0;
        int64_t middleCount = 0;
        int64_t afterCount = 0;

        std::vector<std::vector<float>> before;
        std::vector<std::vector<float>> after;

    public:
        Trim(State *state, int64_t start, int64_t lengthToKeep)
            : Undoable(state), startFrame(start), length(lengthToKeep)
        {
            updateGui = [state = state]
            {
                resetZoom(state);
                state->mainView->setDirty();
            };
        }

        void redo() override
        {
            auto &doc = state->document;
            const int64_t ch = doc.getChannelCount();
            sampleRate = doc.getSampleRate();
            format = doc.getSampleFormat();
            oldTotal = doc.getFrameCount();

            if (startFrame < 0 || length <= 0 || startFrame >= oldTotal)
            {
                return;
            }

            const int64_t endFrame = std::min(startFrame + length, oldTotal);

            beforeCount = startFrame;
            middleCount = endFrame - startFrame;
            afterCount = oldTotal - endFrame;

            before.assign((size_t)ch, {});
            after.assign((size_t)ch, {});

            for (int64_t c = 0; c < ch; ++c)
            {
                if (beforeCount > 0)
                {
                    before[(size_t)c].resize((size_t)beforeCount);
                    for (int64_t i = 0; i < beforeCount; ++i)
                    {
                        before[(size_t)c][(size_t)i] = doc.getSample(c, i);
                    }
                }

                if (afterCount > 0)
                {
                    after[(size_t)c].resize((size_t)afterCount);
                    for (int64_t i = 0; i < afterCount; ++i)
                    {
                        after[(size_t)c][(size_t)i] =
                            doc.getSample(c, endFrame + i);
                    }
                }
            }

            doc.removeFrames(endFrame, afterCount);
            doc.removeFrames(0, beforeCount);
            doc.updateWaveformCache();

            updateCursorPos(state, 0);
            state->selection.setValue1(0);
            state->selection.setValue2(middleCount);
        }

        void undo() override
        {
            auto &doc = state->document;
            const int64_t ch = doc.getChannelCount();

            doc.insertFrames(0, beforeCount);
            for (int64_t c = 0; c < ch; ++c)
            {
                for (int64_t i = 0; i < beforeCount; ++i)
                {
                    doc.setSample(c, i, before[(size_t)c][(size_t)i], false);
                }
            }

            doc.insertFrames(beforeCount + middleCount, afterCount);
            for (int64_t c = 0; c < ch; ++c)
            {
                for (int64_t i = 0; i < afterCount; ++i)
                {
                    doc.setSample(c, beforeCount + middleCount + i,
                                  after[(size_t)c][(size_t)i], false);
                }
            }

            doc.updateWaveformCache();

            updateCursorPos(state, beforeCount);
            state->selection.setValue1(beforeCount);
            state->selection.setValue2(beforeCount + middleCount);
        }

        std::string getUndoDescription() override
        {
            return "Trim";
        }
        std::string getRedoDescription() override
        {
            return "Trim";
        }
    };

} // namespace cupuacu::actions::audio
