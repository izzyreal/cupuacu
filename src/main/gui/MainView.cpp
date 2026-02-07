#include "MainView.hpp"
#include "../State.hpp"
#include "../actions/audio/RecordEdit.hpp"
#include "../actions/audio/RecordedChunkApplier.hpp"
#include "audio/AudioDevices.hpp"
#include "Waveforms.hpp"
#include "Waveform.hpp"
#include "TriangleMarker.hpp"
#include "OpaqueRect.hpp"
#include "Timeline.hpp"

#include <SDL3/SDL.h>
#include <algorithm>

using namespace cupuacu::gui;

void MainView::beginRecordingUndoCaptureIfNeeded(const int64_t startFrame)
{
    if (recordingUndoCapture.active)
    {
        return;
    }

    auto &session = state->activeDocumentSession;
    auto &doc = session.document;

    recordingUndoCapture = {};
    recordingUndoCapture.active = true;
    recordingUndoCapture.startFrame =
        std::clamp<int64_t>(startFrame, 0, doc.getFrameCount());
    recordingUndoCapture.endFrame = recordingUndoCapture.startFrame;
    recordingUndoCapture.oldFrameCount = doc.getFrameCount();
    recordingUndoCapture.oldChannelCount = doc.getChannelCount();
    recordingUndoCapture.targetChannelCount = doc.getChannelCount();
    recordingUndoCapture.oldSampleRate = doc.getSampleRate();
    recordingUndoCapture.oldFormat = static_cast<int>(doc.getSampleFormat());
    recordingUndoCapture.hadOldSelection = session.selection.isActive();
    recordingUndoCapture.oldCursor = session.cursor;
    if (recordingUndoCapture.hadOldSelection)
    {
        recordingUndoCapture.oldSelectionStart = session.selection.getStart();
        recordingUndoCapture.oldSelectionEnd = session.selection.getEnd();
    }

    recordingUndoCapture.overwrittenOldSamples.assign(
        recordingUndoCapture.oldChannelCount, {});
}

void MainView::capturePreOverwriteSamples(const int64_t overlapEndFrame)
{
    if (!recordingUndoCapture.active || recordingUndoCapture.oldChannelCount <= 0)
    {
        return;
    }

    const int64_t boundedOverlapEnd = std::clamp<int64_t>(
        overlapEndFrame, recordingUndoCapture.startFrame,
        recordingUndoCapture.oldFrameCount);
    const int64_t requiredFrames =
        boundedOverlapEnd - recordingUndoCapture.startFrame;
    if (requiredFrames <= recordingUndoCapture.capturedOverwriteFrames)
    {
        return;
    }

    auto &doc = state->activeDocumentSession.document;

    for (int ch = 0; ch < recordingUndoCapture.oldChannelCount; ++ch)
    {
        auto &samples = recordingUndoCapture.overwrittenOldSamples[ch];
        samples.resize(requiredFrames);
        for (int64_t i = recordingUndoCapture.capturedOverwriteFrames;
             i < requiredFrames; ++i)
        {
            samples[i] = doc.getSample(ch, recordingUndoCapture.startFrame + i);
        }
    }

    recordingUndoCapture.capturedOverwriteFrames = requiredFrames;
}

void MainView::captureRecordedChunk(
    const cupuacu::audio::AudioDevices::RecordedChunk &chunk)
{
    if (!recordingUndoCapture.active || chunk.frameCount == 0)
    {
        return;
    }

    auto &doc = state->activeDocumentSession.document;

    const int64_t chunkStart = chunk.startFrame;
    const int64_t chunkEnd = chunk.startFrame + static_cast<int64_t>(chunk.frameCount);
    recordingUndoCapture.endFrame =
        std::max(recordingUndoCapture.endFrame, chunkEnd);
    recordingUndoCapture.targetChannelCount =
        std::max(recordingUndoCapture.targetChannelCount,
                 static_cast<int>(doc.getChannelCount()));
    recordingUndoCapture.newSampleRate = doc.getSampleRate();
    recordingUndoCapture.newFormat = static_cast<int>(doc.getSampleFormat());

    const int64_t recordedFrameCount =
        recordingUndoCapture.endFrame - recordingUndoCapture.startFrame;
    if (recordingUndoCapture.targetChannelCount > 0)
    {
        if ((int)recordingUndoCapture.recordedSamples.size() <
            recordingUndoCapture.targetChannelCount)
        {
            recordingUndoCapture.recordedSamples.resize(
                recordingUndoCapture.targetChannelCount);
        }

        for (int ch = 0; ch < recordingUndoCapture.targetChannelCount; ++ch)
        {
            recordingUndoCapture.recordedSamples[ch].resize(recordedFrameCount);
        }
    }

    for (uint32_t frame = 0; frame < chunk.frameCount; ++frame)
    {
        const int64_t absoluteFrame = chunkStart + static_cast<int64_t>(frame);
        if (absoluteFrame < recordingUndoCapture.startFrame)
        {
            continue;
        }
        const int64_t relativeFrame =
            absoluteFrame - recordingUndoCapture.startFrame;
        if (relativeFrame >= recordedFrameCount)
        {
            continue;
        }

        const std::size_t base =
            static_cast<std::size_t>(frame) *
            cupuacu::audio::AudioDevices::kMaxRecordedChannels;
        const float sampleL = chunk.interleavedSamples[base];
        const float sampleR = chunk.interleavedSamples[base + 1];

        if (recordingUndoCapture.targetChannelCount >= 1)
        {
            recordingUndoCapture.recordedSamples[0][relativeFrame] = sampleL;
        }
        if (recordingUndoCapture.targetChannelCount >= 2)
        {
            recordingUndoCapture.recordedSamples[1][relativeFrame] = sampleR;
        }
    }
}

void MainView::finalizeRecordingUndoCaptureIfComplete()
{
    if (!recordingUndoCapture.active)
    {
        return;
    }
    if (!state->audioDevices || state->audioDevices->isRecording())
    {
        return;
    }

    auto &session = state->activeDocumentSession;
    if (recordingUndoCapture.endFrame <= recordingUndoCapture.startFrame)
    {
        recordingUndoCapture = {};
        return;
    }

    cupuacu::actions::audio::RecordEditData data;
    data.startFrame = recordingUndoCapture.startFrame;
    data.endFrame = recordingUndoCapture.endFrame;
    data.oldFrameCount = recordingUndoCapture.oldFrameCount;
    data.oldChannelCount = recordingUndoCapture.oldChannelCount;
    data.targetChannelCount = recordingUndoCapture.targetChannelCount;
    data.oldSampleRate = recordingUndoCapture.oldSampleRate;
    data.newSampleRate = recordingUndoCapture.newSampleRate;
    data.oldFormat =
        static_cast<SampleFormat>(recordingUndoCapture.oldFormat);
    data.newFormat =
        static_cast<SampleFormat>(recordingUndoCapture.newFormat);
    data.hadOldSelection = recordingUndoCapture.hadOldSelection;
    data.oldSelectionStart = recordingUndoCapture.oldSelectionStart;
    data.oldSelectionEnd = recordingUndoCapture.oldSelectionEnd;
    data.oldCursor = recordingUndoCapture.oldCursor;
    data.hadNewSelection = session.selection.isActive();
    if (data.hadNewSelection)
    {
        data.newSelectionStart = session.selection.getStart();
        data.newSelectionEnd = session.selection.getEnd();
    }
    data.newCursor = session.cursor;
    data.overwrittenOldSamples = std::move(recordingUndoCapture.overwrittenOldSamples);
    data.recordedSamples = std::move(recordingUndoCapture.recordedSamples);

    state->addUndoable(
        std::make_shared<cupuacu::actions::audio::RecordEdit>(state, std::move(data)));
    recordingUndoCapture = {};
}

bool MainView::consumePendingRecordedAudio()
{
    if (!state->audioDevices)
    {
        return false;
    }
    const uint64_t perfStart = SDL_GetPerformanceCounter();
    const uint64_t perfFreq = SDL_GetPerformanceFrequency();
    constexpr double kRecordConsumeBudgetMs = 3.0;
    auto &session = state->activeDocumentSession;
    auto &doc = session.document;

    cupuacu::audio::AudioDevices::RecordedChunk chunk{};
    bool consumedAny = false;
    bool channelLayoutChanged = false;
    bool waveformCacheChanged = false;
    const bool isRecordingNow = state->audioDevices->isRecording();
    const uint64_t maxChunksThisTick = isRecordingNow ? 12 : UINT64_MAX;
    uint64_t chunksThisTick = 0;

    while (true)
    {
        if (chunksThisTick >= maxChunksThisTick)
        {
            break;
        }
        if (isRecordingNow && chunksThisTick > 0)
        {
            const uint64_t now = SDL_GetPerformanceCounter();
            const double elapsedMs =
                perfFreq > 0
                    ? (1000.0 * static_cast<double>(now - perfStart) /
                       static_cast<double>(perfFreq))
                    : 0.0;
            if (elapsedMs >= kRecordConsumeBudgetMs)
            {
                break;
            }
        }
        if (!state->audioDevices->popRecordedChunk(chunk))
        {
            break;
        }

        if (chunk.frameCount == 0 || chunk.channelCount == 0)
        {
            continue;
        }

        ++chunksThisTick;
        consumedAny = true;
        beginRecordingUndoCaptureIfNeeded(chunk.startFrame);
        const int64_t oldFrameCount = doc.getFrameCount();
        const int64_t requiredFrameCount =
            chunk.startFrame + static_cast<int64_t>(chunk.frameCount);

        const int64_t overwriteEnd =
            std::min<int64_t>(requiredFrameCount, oldFrameCount) - 1;
        capturePreOverwriteSamples(overwriteEnd + 1);

        const auto applyResult =
            cupuacu::actions::audio::applyRecordedChunk(doc, chunk);
        waveformCacheChanged =
            waveformCacheChanged || applyResult.waveformCacheChanged;
        channelLayoutChanged =
            channelLayoutChanged || applyResult.channelLayoutChanged;
        captureRecordedChunk(chunk);

        session.cursor = std::max(session.cursor, applyResult.requiredFrameCount);
    }

    if (!consumedAny)
    {
        return false;
    }
    session.syncSelectionAndCursorToDocumentLength();

    if (waveformCacheChanged)
    {
        doc.updateWaveformCache();
    }

    if (channelLayoutChanged ||
        static_cast<int>(state->waveforms.size()) != doc.getChannelCount())
    {
        rebuildWaveforms();
    }
    else
    {
        for (const auto waveform : state->waveforms)
        {
            waveform->updateSamplePoints();
            waveform->setDirty();
        }
    }

    return true;
}

MainView::MainView(State *state) : Component(state, "MainView")
{
    for (int i = 0; i < 4; ++i)
    {
        borders[i] = emplaceChild<OpaqueRect>(state, Colors::border);
    }

    waveforms = emplaceChild<Waveforms>(state);
    timeline = emplaceChild<Timeline>(state);
    rebuildWaveforms();

    cursorTop = borders[0]->emplaceChild<TriangleMarker>(
        state, TriangleMarkerType::CursorTop);
    selStartTop = borders[0]->emplaceChild<TriangleMarker>(
        state, TriangleMarkerType::SelectionStartTop);
    selEndTop = borders[0]->emplaceChild<TriangleMarker>(
        state, TriangleMarkerType::SelectionEndTop);

    cursorBottom = borders[1]->emplaceChild<TriangleMarker>(
        state, TriangleMarkerType::CursorBottom);
    selStartBot = borders[1]->emplaceChild<TriangleMarker>(
        state, TriangleMarkerType::SelectionStartBottom);
    selEndBot = borders[1]->emplaceChild<TriangleMarker>(
        state, TriangleMarkerType::SelectionEndBottom);
}

uint8_t MainView::computeBorderWidth() const
{
    return baseBorderWidth / state->pixelScale;
}

void MainView::rebuildWaveforms() const
{
    waveforms->rebuildWaveforms();
    waveforms->resizeWaveforms();
}

void MainView::resized()
{
    const auto borderWidth = computeBorderWidth();
    const int width = getWidth();
    const int height = getHeight();

    const int timelineHeight = static_cast<int>(60 / state->pixelScale);

    waveforms->setBounds(borderWidth, borderWidth, width - 2 * borderWidth,
                         height - 2 * borderWidth - timelineHeight);

    borders[0]->setBounds(0, 0, width, borderWidth);
    borders[1]->setBounds(0, height - borderWidth, width, borderWidth);
    borders[2]->setBounds(0, borderWidth, borderWidth,
                          height - 2 * borderWidth);
    borders[3]->setBounds(width - borderWidth, borderWidth, borderWidth,
                          height - 2 * borderWidth);

    timeline->setBounds(borderWidth, height - borderWidth - timelineHeight,
                        width - 2 * borderWidth, timelineHeight);

    updateTriangleMarkerBounds();
}

void MainView::updateTriangleMarkerBounds() const

{
    const auto &session = state->activeDocumentSession;
    const auto &viewState = state->mainDocumentSessionWindow->getViewState();
    const auto borderWidth = computeBorderWidth();
    const float triHeight = borderWidth * 0.75f;
    const float halfBase = triHeight;

    const auto sampleOffset = viewState.sampleOffset;
    const auto samplesPerPixel = viewState.samplesPerPixel;

    if (session.selection.isActive())
    {
        cursorTop->setVisible(false);
        cursorBottom->setVisible(false);

        const int32_t startX = Waveform::getXPosForSampleIndex(
            session.selection.getStartInt(), sampleOffset, samplesPerPixel);
        if (startX >= 0 && startX <= waveforms->getWidth())
        {
            selStartTop->setVisible(true);
            selStartBot->setVisible(true);
            selStartTop->setBounds(startX + borderWidth, 0,
                                   static_cast<int>(triHeight + 1.f),
                                   static_cast<int>(triHeight));
            selStartBot->setBounds(startX + borderWidth, 0,
                                   static_cast<int>(triHeight + 1.f),
                                   static_cast<int>(triHeight));
        }
        else
        {
            selStartTop->setVisible(false);
            selStartBot->setVisible(false);
        }

        const int64_t endInclusive = session.selection.getEndInt();
        const int64_t endToUse = endInclusive + 1;
        const int32_t endX = Waveform::getXPosForSampleIndex(
            endToUse, sampleOffset, samplesPerPixel);

        if (endX >= 0 && endX <= waveforms->getWidth())
        {
            selEndTop->setVisible(true);
            selEndBot->setVisible(true);
            selEndTop->setBounds(
                endX + borderWidth - static_cast<int>(triHeight), 0,
                static_cast<int>(triHeight), static_cast<int>(triHeight));
            selEndBot->setBounds(
                endX + borderWidth - static_cast<int>(triHeight), 0,
                static_cast<int>(triHeight), static_cast<int>(triHeight));
        }
        else
        {
            selEndTop->setVisible(false);
            selEndBot->setVisible(false);
        }
    }
    else
    {
        selStartTop->setVisible(false);
        selStartBot->setVisible(false);
        selEndTop->setVisible(false);
        selEndBot->setVisible(false);

        const int32_t xPos = Waveform::getXPosForSampleIndex(
            session.cursor, sampleOffset, samplesPerPixel);
        if (xPos >= 0 && xPos <= waveforms->getWidth())
        {
            const int cursorX = xPos + borderWidth;
            cursorTop->setVisible(true);
            cursorBottom->setVisible(true);
            cursorTop->setBounds(cursorX - static_cast<int>(halfBase) + 1, 0,
                                 static_cast<int>(halfBase * 2),
                                 static_cast<int>(triHeight));
            cursorBottom->setBounds(cursorX - static_cast<int>(halfBase) + 1, 0,
                                    static_cast<int>(halfBase * 2),
                                    static_cast<int>(triHeight));
        }
        else
        {
            cursorTop->setVisible(false);
            cursorBottom->setVisible(false);
        }
    }
}

void MainView::timerCallback()
{
    const auto &session = state->activeDocumentSession;
    const auto &viewState = state->mainDocumentSessionWindow->getViewState();
    const bool consumedRecordedAudio = consumePendingRecordedAudio();
    const bool isRecordingNow =
        state->audioDevices && state->audioDevices->isRecording();
    if (!isRecordingNow && wasRecordingLastTick)
    {
        finalizeRecordingUndoCaptureIfComplete();
    }
    wasRecordingLastTick = isRecordingNow;

    if (session.cursor != lastDrawnCursor ||
        session.selection.isActive() != lastSelectionIsActive ||
        viewState.samplesPerPixel != lastSamplesPerPixel ||
        viewState.sampleOffset != lastSampleOffset ||
        session.selection.getStartInt() != lastSelectionStart ||
        session.selection.getEndInt() != lastSelectionEnd ||
        consumedRecordedAudio)
    {
        lastDrawnCursor = session.cursor;
        lastSelectionIsActive = session.selection.isActive();
        lastSamplesPerPixel = viewState.samplesPerPixel;
        lastSampleOffset = viewState.sampleOffset;
        lastSelectionStart = session.selection.getStartInt();
        lastSelectionEnd = session.selection.getEndInt();

        updateTriangleMarkerBounds();
        setDirty();
    }
}
