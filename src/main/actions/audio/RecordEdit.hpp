#pragma once

#include "DurationMutationUndoable.hpp"

#include "../../Document.hpp"
#include "../../gui/MainView.hpp"
#include "../../gui/Waveform.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace cupuacu::actions::audio
{
    struct RecordEditData
    {
        int64_t startFrame = 0;
        int64_t endFrame = 0;
        int64_t oldFrameCount = 0;
        int oldChannelCount = 0;
        int targetChannelCount = 0;

        int oldSampleRate = 0;
        int newSampleRate = 0;
        SampleFormat oldFormat = SampleFormat::Unknown;
        SampleFormat newFormat = SampleFormat::Unknown;

        bool hadOldSelection = false;
        bool hadNewSelection = false;
        double oldSelectionStart = 0.0;
        double oldSelectionEnd = 0.0;
        double newSelectionStart = 0.0;
        double newSelectionEnd = 0.0;
        int64_t oldCursor = 0;
        int64_t newCursor = 0;

        std::vector<std::vector<float>> overwrittenOldSamples;
        std::vector<std::vector<float>> recordedSamples;
    };

    class RecordEdit : public DurationMutationUndoable
    {
    public:
        RecordEdit(State *stateToUse, RecordEditData dataToUse)
            : DurationMutationUndoable(stateToUse), data(std::move(dataToUse))
        {
        }

        void redo() override
        {
            auto &session = state->activeDocumentSession;
            auto &doc = session.document;

            if (data.targetChannelCount <= 0 || data.endFrame <= data.startFrame)
            {
                return;
            }

            if (doc.getChannelCount() == 0 && data.targetChannelCount > 0)
            {
                doc.initialize(data.newFormat, data.newSampleRate,
                               data.targetChannelCount, data.oldFrameCount);
            }
            else if (doc.getChannelCount() < data.targetChannelCount)
            {
                doc.resizeBuffer(data.targetChannelCount, doc.getFrameCount());
            }

            if (doc.getFrameCount() < data.oldFrameCount)
            {
                doc.insertFrames(doc.getFrameCount(),
                                 data.oldFrameCount - doc.getFrameCount());
            }

            if (doc.getFrameCount() < data.endFrame)
            {
                doc.insertFrames(doc.getFrameCount(),
                                 data.endFrame - doc.getFrameCount());
            }

            const int64_t recordedFrameCount = data.endFrame - data.startFrame;
            for (int ch = 0;
                 ch < doc.getChannelCount() && ch < (int)data.recordedSamples.size();
                 ++ch)
            {
                const auto &samples = data.recordedSamples[ch];
                const int64_t framesToWrite = std::min<int64_t>(
                    recordedFrameCount, static_cast<int64_t>(samples.size()));
                for (int64_t i = 0; i < framesToWrite; ++i)
                {
                    doc.setSample(ch, data.startFrame + i, samples[i], false);
                }
                if (framesToWrite > 0)
                {
                    doc.getWaveformCache(ch).invalidateSamples(
                        data.startFrame, data.startFrame + framesToWrite - 1);
                }
            }

            doc.updateWaveformCache();
            session.syncSelectionAndCursorToDocumentLength();
            restoreNewSessionState();
        }

        void undo() override
        {
            auto &session = state->activeDocumentSession;
            auto &doc = session.document;

            if (data.endFrame <= data.startFrame)
            {
                return;
            }

            if (data.oldFrameCount < doc.getFrameCount() &&
                data.endFrame > data.oldFrameCount)
            {
                const int64_t removeCount = std::min<int64_t>(
                    data.endFrame - data.oldFrameCount,
                    doc.getFrameCount() - data.oldFrameCount);
                if (removeCount > 0)
                {
                    doc.removeFrames(data.oldFrameCount, removeCount);
                }
            }

            const int64_t overlapEnd = std::min<int64_t>(data.oldFrameCount,
                                                         data.endFrame);
            const int64_t overlapFrameCount =
                std::max<int64_t>(0, overlapEnd - data.startFrame);

            for (int ch = 0;
                 ch < data.oldChannelCount && ch < doc.getChannelCount() &&
                 ch < (int)data.overwrittenOldSamples.size();
                 ++ch)
            {
                const auto &samples = data.overwrittenOldSamples[ch];
                const int64_t framesToRestore = std::min<int64_t>(
                    overlapFrameCount, static_cast<int64_t>(samples.size()));
                for (int64_t i = 0; i < framesToRestore; ++i)
                {
                    doc.setSample(ch, data.startFrame + i, samples[i], false);
                }
                if (framesToRestore > 0)
                {
                    doc.getWaveformCache(ch).invalidateSamples(
                        data.startFrame, data.startFrame + framesToRestore - 1);
                }
            }

            if (doc.getChannelCount() > data.oldChannelCount)
            {
                if (data.oldChannelCount == 0)
                {
                    doc.initialize(data.oldFormat, data.oldSampleRate, 0, 0);
                }
                else
                {
                    doc.resizeBuffer(data.oldChannelCount, doc.getFrameCount());
                }
            }

            doc.updateWaveformCache();
            session.syncSelectionAndCursorToDocumentLength();
            restoreOldSessionState();
        }

        std::string getUndoDescription() override
        {
            return "Record";
        }

        std::string getRedoDescription() override
        {
            return "Record";
        }

    private:
        RecordEditData data;

        void afterDurationMutationUi() override
        {
            if (!state || !state->mainView)
            {
                return;
            }

            const auto currentChannelCount =
                state->activeDocumentSession.document.getChannelCount();
            const auto currentWaveformCount =
                static_cast<int64_t>(state->waveforms.size());
            if (currentWaveformCount != currentChannelCount)
            {
                state->mainView->rebuildWaveforms();
            }
        }

        void restoreOldSessionState() const
        {
            auto &session = state->activeDocumentSession;
            if (data.hadOldSelection)
            {
                session.selection.setValue1(data.oldSelectionStart);
                session.selection.setValue2(data.oldSelectionEnd);
            }
            else
            {
                session.selection.reset();
            }

            updateCursorPos(state, data.oldCursor);
        }

        void restoreNewSessionState() const
        {
            auto &session = state->activeDocumentSession;
            if (data.hadNewSelection)
            {
                session.selection.setValue1(data.newSelectionStart);
                session.selection.setValue2(data.newSelectionEnd);
            }
            else
            {
                session.selection.reset();
            }

            updateCursorPos(state, data.newCursor);
        }
    };
} // namespace cupuacu::actions::audio
