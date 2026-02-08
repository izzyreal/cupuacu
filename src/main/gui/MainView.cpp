#include "MainView.hpp"
#include "../State.hpp"
#include "../actions/audio/RecordEdit.hpp"
#include "../actions/audio/RecordedChunkApplier.hpp"
#include "audio/AudioDevices.hpp"
#include "Waveforms.hpp"
#include "Waveform.hpp"
#include "WaveformsUnderlay.hpp"
#include "TriangleMarker.hpp"
#include "OpaqueRect.hpp"
#include "ScrollBar.hpp"
#include "Timeline.hpp"
#include "WaveformRefresh.hpp"
#include "playback/PlaybackRange.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>

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

bool MainView::followTransportHead()
{
    if (!state->audioDevices || !state->mainDocumentSessionWindow || !waveforms)
    {
        return false;
    }

    const auto snapshot = state->audioDevices->getSnapshot();
    const int64_t transportHead =
        snapshot.isRecording()
            ? snapshot.getRecordingPosition()
            : (snapshot.isPlaying() ? snapshot.getPlaybackPosition() : -1);

    if (transportHead < 0)
    {
        return false;
    }

    bool changed = false;

    auto &viewState = state->mainDocumentSessionWindow->getViewState();
    if (viewState.samplesPerPixel <= 0.0 || waveforms->getWidth() <= 0)
    {
        return changed;
    }

    const int64_t oldOffset = viewState.sampleOffset;
    const int waveformWidth = waveforms->getWidth();
    int64_t newOffset = oldOffset;
    const int32_t xPos = Waveform::getXPosForSampleIndex(
        transportHead, oldOffset, viewState.samplesPerPixel);

    if (xPos < 0)
    {
        const int32_t deficitPixels = -xPos;
        const int64_t deltaSamples = std::max<int64_t>(
            1, static_cast<int64_t>(
                   std::ceil(static_cast<double>(deficitPixels) *
                             viewState.samplesPerPixel)));
        newOffset = oldOffset - deltaSamples;
    }
    else if (xPos >= waveformWidth)
    {
        const int32_t overflowPixels = xPos - (waveformWidth - 1);
        const int64_t deltaSamples = std::max<int64_t>(
            1, static_cast<int64_t>(
                   std::ceil(static_cast<double>(overflowPixels) *
                             viewState.samplesPerPixel)));
        newOffset = oldOffset + deltaSamples;
    }

    if (newOffset != oldOffset)
    {
        updateSampleOffset(state, newOffset);
    }

    if (viewState.sampleOffset != oldOffset)
    {
        changed = true;
        refreshWaveforms(state, false, true);
    }

    return changed;
}

MainView::MainView(State *state) : Component(state, "MainView")
{
    for (int i = 0; i < 4; ++i)
    {
        borders[i] = emplaceChild<OpaqueRect>(state, Colors::border);
    }

    horizontalScrollBar = borders[0]->emplaceChild<ScrollBar>(
        state, ScrollBar::Orientation::Horizontal,
        [state]()
        {
            return static_cast<double>(
                state->mainDocumentSessionWindow->getViewState().sampleOffset);
        },
        []() { return 0.0; },
        [state]() { return static_cast<double>(getMaxSampleOffset(state)); },
        [this, state]()
        {
            return std::max(
                1.0, static_cast<double>(waveforms->getWidth()) *
                         state->mainDocumentSessionWindow->getViewState()
                             .samplesPerPixel);
        },
        [state](const double value)
        {
            auto &viewState = state->mainDocumentSessionWindow->getViewState();
            const int64_t oldOffset = viewState.sampleOffset;
            updateSampleOffset(state, static_cast<int64_t>(std::llround(value)));
            if (oldOffset == viewState.sampleOffset)
            {
                return;
            }

            resetSampleValueUnderMouseCursor(state);
            clearWaveformHighlights(state);
            refreshWaveforms(state, true, true);
        });

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
    const int scrollBarHeight = std::max(8, static_cast<int>(14 / state->pixelScale));

    horizontalScrollBar->setBounds(borderWidth, 0,
                                   width - 2 * borderWidth, scrollBarHeight);
    waveforms->setBounds(borderWidth, borderWidth + scrollBarHeight,
                         width - 2 * borderWidth,
                         height - 2 * borderWidth - timelineHeight -
                             scrollBarHeight);

    borders[0]->setBounds(0, 0, width, borderWidth + scrollBarHeight);
    borders[1]->setBounds(0, height - borderWidth, width, borderWidth);
    borders[2]->setBounds(0, borderWidth + scrollBarHeight, borderWidth,
                          height - 2 * borderWidth - scrollBarHeight);
    borders[3]->setBounds(width - borderWidth, borderWidth + scrollBarHeight,
                          borderWidth,
                          height - 2 * borderWidth - scrollBarHeight);

    timeline->setBounds(borderWidth, height - borderWidth - timelineHeight,
                        width - 2 * borderWidth, timelineHeight);

    updateTriangleMarkerBounds();
}

void MainView::updateTriangleMarkerBounds() const

{
    const auto &session = state->activeDocumentSession;
    const auto &viewState = state->mainDocumentSessionWindow->getViewState();
    const auto borderWidth = computeBorderWidth();
    const int scrollBarHeight =
        std::max(8, static_cast<int>(14 / state->pixelScale));
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
            selStartTop->setBounds(startX + borderWidth, scrollBarHeight,
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
                endX + borderWidth - static_cast<int>(triHeight), scrollBarHeight,
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
            cursorTop->setBounds(cursorX - static_cast<int>(halfBase) + 1,
                                 scrollBarHeight,
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
    auto &session = state->activeDocumentSession;
    auto &viewState = state->mainDocumentSessionWindow->getViewState();
    const bool consumedRecordedAudio = consumePendingRecordedAudio();
    const bool followedTransport = followTransportHead();
    const bool isPlayingNow =
        state->audioDevices && state->audioDevices->isPlaying();
    const bool isRecordingNow =
        state->audioDevices && state->audioDevices->isRecording();
    const int64_t playbackPositionNow =
        (isPlayingNow && state->audioDevices)
            ? state->audioDevices->getPlaybackPosition()
            : -1;
    for (auto *waveform : state->waveforms)
    {
        waveform->setPlaybackPosition(playbackPositionNow);
    }
    bool selectionInteractionActive = false;
    if (state->mainDocumentSessionWindow &&
        state->mainDocumentSessionWindow->getWindow())
    {
        auto *capturing =
            state->mainDocumentSessionWindow->getWindow()->getCapturingComponent();
        selectionInteractionActive =
            dynamic_cast<WaveformsUnderlay *>(capturing) != nullptr ||
            dynamic_cast<TriangleMarker *>(capturing) != nullptr;
    }

    const bool selectionActive = session.selection.isActive();
    const int64_t selectionStart = session.selection.getStartInt();
    const int64_t selectionEnd = session.selection.getEndInt();

    if (isPlayingNow && state->audioDevices && !selectionInteractionActive)
    {
        const auto range = cupuacu::playback::computeRangeForLiveUpdate(
            session, state->loopPlaybackEnabled, state->playbackRangeStart,
            state->playbackRangeEnd);
        const uint64_t start = range.start;
        const uint64_t end = range.end;

        const bool shouldUpdatePlayback =
            start != lastPlaybackUpdateStart || end != lastPlaybackUpdateEnd ||
            selectionActive != lastPlaybackUpdateSelectionActive ||
            viewState.selectedChannels != lastPlaybackUpdateChannels ||
            state->loopPlaybackEnabled != lastPlaybackLoopEnabled;

        if (shouldUpdatePlayback)
        {
            cupuacu::audio::UpdatePlayback updateMsg{};
            updateMsg.startPos = start;
            updateMsg.endPos = end;
            updateMsg.loopEnabled = state->loopPlaybackEnabled;
            updateMsg.selectionIsActive = selectionActive;
            updateMsg.selectedChannels = viewState.selectedChannels;
            state->audioDevices->enqueue(updateMsg);

            lastPlaybackUpdateStart = start;
            lastPlaybackUpdateEnd = end;
            lastPlaybackUpdateSelectionActive = selectionActive;
            lastPlaybackUpdateChannels = viewState.selectedChannels;
            lastPlaybackLoopEnabled = state->loopPlaybackEnabled;
        }
    }
    else
    {
        lastPlaybackUpdateStart = state->playbackRangeStart;
        lastPlaybackUpdateEnd = state->playbackRangeEnd;
        lastPlaybackUpdateSelectionActive = selectionActive;
        lastPlaybackUpdateChannels = viewState.selectedChannels;
        lastPlaybackLoopEnabled = state->loopPlaybackEnabled;
    }

    if (!isRecordingNow && wasRecordingLastTick)
    {
        finalizeRecordingUndoCaptureIfComplete();
    }
    wasRecordingLastTick = isRecordingNow;

    if (session.cursor != lastDrawnCursor ||
        selectionActive != lastSelectionIsActive ||
        viewState.samplesPerPixel != lastSamplesPerPixel ||
        viewState.sampleOffset != lastSampleOffset ||
        selectionStart != lastSelectionStart || selectionEnd != lastSelectionEnd ||
        consumedRecordedAudio || followedTransport)
    {
        lastDrawnCursor = session.cursor;
        lastSelectionIsActive = selectionActive;
        lastSamplesPerPixel = viewState.samplesPerPixel;
        lastSampleOffset = viewState.sampleOffset;
        lastSelectionStart = selectionStart;
        lastSelectionEnd = selectionEnd;

        updateTriangleMarkerBounds();
        setDirty();
    }
}
