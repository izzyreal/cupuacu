#pragma once
#include "../Undoable.hpp"
#include "../../Document.hpp"
#include "../../gui/MainView.hpp"
#include "../Zoom.hpp"
#include <vector>
#include <cstdint>
#include <algorithm>
#include <cmath>

namespace cupuacu::actions::audio
{

    class Cut : public Undoable
    {
        int64_t startFrame;
        int64_t numFrames;

        std::vector<std::vector<float>> removed;

        double oldSel1 = 0.0;
        double oldSel2 = 0.0;
        int64_t oldCursorPos = 0;

    public:
        Cut(State *state, int64_t start, int64_t count)
            : Undoable(state), startFrame(start), numFrames(count)
        {
            auto &session = state->activeDocumentSession;
            if (session.selection.isActive())
            {
                oldSel1 = session.selection.getStart();
                oldSel2 = session.selection.getEnd();
            }

            oldCursorPos = session.cursor;

            updateGui = [state = state]
            {
                auto &session = state->activeDocumentSession;
                const auto frameCount =
                    std::max<int64_t>(0, session.document.getFrameCount());
                const auto waveformWidth =
                    static_cast<double>(gui::Waveform::getWaveformWidth(state));
                const auto &viewState =
                    state->mainDocumentSessionWindow->getViewState();

                // If the current zoom would leave unused space on the right
                // after this cut, reset zoom so waveform fills the viewport.
                const bool shouldResetZoomToFillWidth =
                    frameCount > 0 && waveformWidth > 0.0 &&
                    std::ceil(waveformWidth * viewState.samplesPerPixel) >
                        static_cast<double>(frameCount);

                if (shouldResetZoomToFillWidth)
                {
                    resetZoomAndRefreshWaveforms(state);
                    state->mainView->setDirty();
                    return;
                }

                if (state->mainDocumentSessionWindow)
                {
                    auto &viewState =
                        state->mainDocumentSessionWindow->getViewState();
                    updateSampleOffset(state, viewState.sampleOffset);
                }
                gui::Waveform::updateAllSamplePoints(state);
                gui::Waveform::setAllWaveformsDirty(state);
                state->mainView->setDirty();
            };
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
