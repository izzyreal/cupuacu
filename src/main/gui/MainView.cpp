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
#include "MainViewLayoutPlanning.hpp"
#include "MainViewSelectionMarkerPlanning.hpp"
#include "UiScale.hpp"
#include "WaveformRefresh.hpp"
#include "WaveformCache.hpp"
#include "playback/PlaybackRange.hpp"
#include "Helpers.hpp"
#include "Colors.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <limits>

using namespace cupuacu::gui;

namespace
{
    int computeScrollBarHeightForScale(const cupuacu::State *state)
    {
        return cupuacu::gui::scaleUi(state, 14.0f);
    }
} // namespace

void MainView::beginRecordingUndoCaptureIfNeeded(const int64_t startFrame)
{
    if (recordingUndoCapture.active)
    {
        return;
    }

    auto &session = state->getActiveDocumentSession();
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

    auto &doc = state->getActiveDocumentSession().document;

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

    auto &doc = state->getActiveDocumentSession().document;

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

    auto &session = state->getActiveDocumentSession();
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

bool MainView::shouldKeepConsumingRecordedAudio(const bool isRecordingNow,
                                                const uint64_t chunksThisTick,
                                                const uint64_t perfStart,
                                                const uint64_t perfFreq) const
{
    constexpr double kRecordConsumeBudgetMs = 3.0;
    const uint64_t maxChunksThisTick = isRecordingNow ? 12 : UINT64_MAX;
    if (chunksThisTick >= maxChunksThisTick)
    {
        return false;
    }

    if (!isRecordingNow || chunksThisTick == 0)
    {
        return true;
    }

    const uint64_t now = SDL_GetPerformanceCounter();
    const double elapsedMs =
        perfFreq > 0
            ? (1000.0 * static_cast<double>(now - perfStart) /
               static_cast<double>(perfFreq))
            : 0.0;
    return elapsedMs < kRecordConsumeBudgetMs;
}

void MainView::applyRecordedChunkToSession(
    const cupuacu::audio::AudioDevices::RecordedChunk &chunk,
    bool &channelLayoutChanged, bool &waveformCacheChanged)
{
    auto &session = state->getActiveDocumentSession();
    auto &doc = session.document;

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

void MainView::refreshWaveformsAfterRecordedAudio(
    const bool channelLayoutChanged, const bool waveformCacheChanged)
{
    auto &doc = state->getActiveDocumentSession().document;

    if (waveformCacheChanged)
    {
        doc.updateWaveformCache();
    }

    if (channelLayoutChanged ||
        static_cast<int>(state->waveforms.size()) != doc.getChannelCount())
    {
        rebuildWaveforms();
        return;
    }

    for (const auto waveform : state->waveforms)
    {
        waveform->updateSamplePoints();
        waveform->setDirty();
    }
}

bool MainView::consumePendingRecordedAudio()
{
    if (!state->audioDevices)
    {
        return false;
    }
    const uint64_t perfStart = SDL_GetPerformanceCounter();
    const uint64_t perfFreq = SDL_GetPerformanceFrequency();
    auto &session = state->getActiveDocumentSession();

    cupuacu::audio::AudioDevices::RecordedChunk chunk{};
    bool consumedAny = false;
    bool channelLayoutChanged = false;
    bool waveformCacheChanged = false;
    const bool isRecordingNow = state->audioDevices->isRecording();
    uint64_t chunksThisTick = 0;

    while (true)
    {
        if (!shouldKeepConsumingRecordedAudio(isRecordingNow, chunksThisTick,
                                              perfStart, perfFreq))
        {
            break;
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
        applyRecordedChunkToSession(chunk, channelLayoutChanged,
                                    waveformCacheChanged);
    }

    if (!consumedAny)
    {
        return false;
    }
    session.syncSelectionAndCursorToDocumentLength();
    refreshWaveformsAfterRecordedAudio(channelLayoutChanged,
                                       waveformCacheChanged);
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

    auto &viewState = state->getActiveViewState();
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

int64_t MainView::getPlaybackPositionForWaveforms() const
{
    if (!state->audioDevices || !state->audioDevices->isPlaying())
    {
        return -1;
    }

    return state->audioDevices->getPlaybackPosition();
}

void MainView::updateWaveformPlaybackPositions() const
{
    const int64_t playbackPosition = getPlaybackPositionForWaveforms();
    for (auto *waveform : state->waveforms)
    {
        waveform->setPlaybackPosition(playbackPosition);
    }
}

bool MainView::isSelectionInteractionActive() const
{
    if (!state->mainDocumentSessionWindow ||
        !state->mainDocumentSessionWindow->getWindow())
    {
        return false;
    }

    auto *capturing =
        state->mainDocumentSessionWindow->getWindow()->getCapturingComponent();
    return dynamic_cast<WaveformsUnderlay *>(capturing) != nullptr ||
           dynamic_cast<TriangleMarker *>(capturing) != nullptr;
}

void MainView::syncLivePlaybackRange(const bool selectionActive,
                                     const SelectedChannels selectedChannels)
{
    const bool isPlayingNow =
        state->audioDevices && state->audioDevices->isPlaying();

    if (isPlayingNow && state->audioDevices && !isSelectionInteractionActive())
    {
        const auto range = cupuacu::playback::computeRangeForLiveUpdate(
            state->getActiveDocumentSession(), state->loopPlaybackEnabled,
            state->playbackRangeStart, state->playbackRangeEnd);
        const uint64_t start = range.start;
        const uint64_t end = range.end;

        const bool shouldUpdatePlayback =
            start != lastPlaybackUpdateStart || end != lastPlaybackUpdateEnd ||
            selectionActive != lastPlaybackUpdateSelectionActive ||
            selectedChannels != lastPlaybackUpdateChannels ||
            state->loopPlaybackEnabled != lastPlaybackLoopEnabled;

        if (shouldUpdatePlayback)
        {
            cupuacu::audio::UpdatePlayback updateMsg{};
            updateMsg.startPos = start;
            updateMsg.endPos = end;
            updateMsg.loopEnabled = state->loopPlaybackEnabled;
            updateMsg.selectionIsActive = selectionActive;
            updateMsg.selectedChannels = selectedChannels;
            state->audioDevices->enqueue(updateMsg);

            lastPlaybackUpdateStart = start;
            lastPlaybackUpdateEnd = end;
            lastPlaybackUpdateSelectionActive = selectionActive;
            lastPlaybackUpdateChannels = selectedChannels;
            lastPlaybackLoopEnabled = state->loopPlaybackEnabled;
        }

        return;
    }

    lastPlaybackUpdateStart = state->playbackRangeStart;
    lastPlaybackUpdateEnd = state->playbackRangeEnd;
    lastPlaybackUpdateSelectionActive = selectionActive;
    lastPlaybackUpdateChannels = selectedChannels;
    lastPlaybackLoopEnabled = state->loopPlaybackEnabled;
}

bool MainView::shouldRefreshMarkerBounds(const bool consumedRecordedAudio,
                                         const bool followedTransport,
                                         const bool selectionActive,
                                         const int64_t selectionStart,
                                         const int64_t selectionEnd) const
{
    const auto &session = state->getActiveDocumentSession();
    const auto &viewState = state->getActiveViewState();

    return session.cursor != lastDrawnCursor ||
           selectionActive != lastSelectionIsActive ||
           viewState.samplesPerPixel != lastSamplesPerPixel ||
           viewState.sampleOffset != lastSampleOffset ||
           selectionStart != lastSelectionStart ||
           selectionEnd != lastSelectionEnd || consumedRecordedAudio ||
           followedTransport;
}

void MainView::rememberMarkerInputs(const bool selectionActive,
                                    const int64_t selectionStart,
                                    const int64_t selectionEnd)
{
    const auto &session = state->getActiveDocumentSession();
    const auto &viewState = state->getActiveViewState();

    lastDrawnCursor = session.cursor;
    lastSelectionIsActive = selectionActive;
    lastSamplesPerPixel = viewState.samplesPerPixel;
    lastSampleOffset = viewState.sampleOffset;
    lastSelectionStart = selectionStart;
    lastSelectionEnd = selectionEnd;
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
                state->getActiveViewState().sampleOffset);
        },
        []() { return 0.0; },
        [state]() { return static_cast<double>(getMaxSampleOffset(state)); },
        [this, state]()
        {
            return std::max(
                1.0, static_cast<double>(waveforms->getWidth()) *
                         state->getActiveViewState()
                             .samplesPerPixel);
        },
        [state](const double value)
        {
            auto &viewState = state->getActiveViewState();
            const int64_t oldOffset = viewState.sampleOffset;
            const int64_t requestedOffset =
                static_cast<int64_t>(std::llround(value));
            const int64_t snappedOffset =
                Waveform::quantizeBlockScrollOffset(
                    requestedOffset, getMaxSampleOffset(state),
                    viewState.samplesPerPixel, state->pixelScale);
            updateSampleOffset(state, snappedOffset);
            if (oldOffset == viewState.sampleOffset)
            {
                return;
            }

            refreshWaveformsAfterViewChange(
                state,
                Waveform::shouldShowSamplePoints(viewState.samplesPerPixel,
                                                state->pixelScale),
                true);
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

void MainView::onDraw(SDL_Renderer *renderer)
{
    Helpers::fillRect(renderer, getLocalBounds(), Colors::black);
}

uint8_t MainView::computeBorderWidth() const
{
    return scaleUi(state, static_cast<float>(baseBorderWidth));
}

void MainView::rebuildWaveforms() const
{
    waveforms->rebuildWaveforms();
    waveforms->resizeWaveforms();
}

void MainView::resized()
{
    const int width = getWidth();
    const int height = getHeight();
    const auto plan =
        planMainViewLayout(width, height, state->uiScale, state->pixelScale);

    horizontalScrollBar->setBounds(plan.horizontalScrollBar.x,
                                   plan.horizontalScrollBar.y,
                                   plan.horizontalScrollBar.w,
                                   plan.horizontalScrollBar.h);
    waveforms->setBounds(plan.waveforms.x, plan.waveforms.y, plan.waveforms.w,
                         plan.waveforms.h);

    borders[0]->setBounds(plan.topBorder.x, plan.topBorder.y, plan.topBorder.w,
                          plan.topBorder.h);
    borders[1]->setBounds(plan.bottomBorder.x, plan.bottomBorder.y,
                          plan.bottomBorder.w, plan.bottomBorder.h);
    borders[2]->setBounds(plan.leftBorder.x, plan.leftBorder.y,
                          plan.leftBorder.w, plan.leftBorder.h);
    borders[3]->setBounds(plan.rightBorder.x, plan.rightBorder.y,
                          plan.rightBorder.w, plan.rightBorder.h);

    timeline->setBounds(plan.timeline.x, plan.timeline.y, plan.timeline.w,
                        plan.timeline.h);

    updateTriangleMarkerBounds();
}

void MainView::updateTriangleMarkerBounds() const

{
    const auto &session = state->getActiveDocumentSession();
    const auto &viewState = state->getActiveViewState();
    const auto borderWidth = computeBorderWidth();
    const int scrollBarHeight =
        computeScrollBarHeightForScale(state);
    const float triHeight = borderWidth * 0.75f;
    const float halfBase = triHeight;

    const auto sampleOffset = viewState.sampleOffset;
    const auto samplesPerPixel = viewState.samplesPerPixel;

    if (samplesPerPixel <= 0.0 || waveforms->getWidth() <= 0)
    {
        cursorTop->setVisible(false);
        cursorBottom->setVisible(false);
        selStartTop->setVisible(false);
        selStartBot->setVisible(false);
        selEndTop->setVisible(false);
        selEndBot->setVisible(false);
        return;
    }

    if (session.selection.isActive())
    {
        cursorTop->setVisible(false);
        cursorBottom->setVisible(false);

        int32_t startX = 0;
        int32_t endX = 0;
        if (samplesPerPixel >= 1.0)
        {
            const int64_t firstSample = session.selection.getStartInt();
            const int64_t lastSampleExclusive =
                session.selection.getEndExclusiveInt();

            if (Waveform::computeBlockModeSelectionFillEdgePixels(
                    firstSample, lastSampleExclusive, sampleOffset,
                    samplesPerPixel, waveforms->getWidth(), startX,
                    endX) == false &&
                Waveform::computeBlockModeSelectionEdgePixels(
                    firstSample, lastSampleExclusive, sampleOffset,
                    samplesPerPixel, waveforms->getWidth(), startX, endX, 1,
                    false) == false)
            {
                startX = std::numeric_limits<int32_t>::min();
                endX = std::numeric_limits<int32_t>::min();
            }
        }
        else
        {
            startX = Waveform::getXPosForSampleIndex(
                session.selection.getStartInt(), sampleOffset, samplesPerPixel);
            const int64_t endToUse = session.selection.getEndExclusiveInt();
            endX = Waveform::getXPosForSampleIndex(endToUse, sampleOffset,
                                                   samplesPerPixel);
        }

        if (startX >= 0 && startX <= waveforms->getWidth())
        {
            const auto topPlan = planTopSelectionStartMarker(
                startX, borderWidth, scrollBarHeight, triHeight);
            const auto bottomPlan =
                planBottomSelectionStartMarker(startX, borderWidth, triHeight);
            selStartTop->setVisible(topPlan.visible);
            selStartBot->setVisible(bottomPlan.visible);
            selStartTop->setBounds(topPlan.rect);
            selStartBot->setBounds(bottomPlan.rect);
        }
        else
        {
            selStartTop->setVisible(false);
            selStartBot->setVisible(false);
        }

        if (endX >= 0 && endX <= waveforms->getWidth())
        {
            const auto topPlan = planTopSelectionEndMarker(
                endX, borderWidth, scrollBarHeight, triHeight);
            const auto bottomPlan =
                planBottomSelectionEndMarker(endX, borderWidth, triHeight);
            selEndTop->setVisible(topPlan.visible);
            selEndBot->setVisible(bottomPlan.visible);
            selEndTop->setBounds(topPlan.rect);
            selEndBot->setBounds(bottomPlan.rect);
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
            const auto topPlan = planTopCursorMarker(
                xPos, borderWidth, scrollBarHeight, halfBase, triHeight);
            const auto bottomPlan =
                planBottomCursorMarker(xPos, borderWidth, halfBase, triHeight);
            cursorTop->setVisible(topPlan.visible);
            cursorBottom->setVisible(bottomPlan.visible);
            cursorTop->setBounds(topPlan.rect);
            cursorBottom->setBounds(bottomPlan.rect);
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
    auto &session = state->getActiveDocumentSession();
    auto &viewState = state->getActiveViewState();
    const bool consumedRecordedAudio = consumePendingRecordedAudio();
    const bool followedTransport = followTransportHead();
    const bool isRecordingNow =
        state->audioDevices && state->audioDevices->isRecording();
    updateWaveformPlaybackPositions();

    const bool selectionActive = session.selection.isActive();
    const int64_t selectionStart = session.selection.getStartInt();
    const int64_t selectionEnd = session.selection.getEndInt();

    syncLivePlaybackRange(selectionActive, viewState.selectedChannels);

    if (!isRecordingNow && wasRecordingLastTick)
    {
        finalizeRecordingUndoCaptureIfComplete();
    }
    wasRecordingLastTick = isRecordingNow;

    if (shouldRefreshMarkerBounds(consumedRecordedAudio, followedTransport,
                                  selectionActive, selectionStart,
                                  selectionEnd))
    {
        rememberMarkerInputs(selectionActive, selectionStart, selectionEnd);
        updateTriangleMarkerBounds();
        setDirty();
    }
}
