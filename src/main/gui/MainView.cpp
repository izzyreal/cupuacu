#include "MainView.hpp"
#include "../State.hpp"
#include "audio/AudioDevices.hpp"
#include "Waveforms.hpp"
#include "Waveform.hpp"
#include "TriangleMarker.hpp"
#include "OpaqueRect.hpp"
#include "Timeline.hpp"

#include <algorithm>

using namespace cupuacu::gui;

bool MainView::consumePendingRecordedAudio()
{
    if (!state->audioDevices)
    {
        return false;
    }

    cupuacu::audio::AudioDevices::RecordedChunk chunk{};
    bool consumedAny = false;
    bool channelLayoutChanged = false;
    bool waveformCacheChanged = false;

    while (state->audioDevices->popRecordedChunk(chunk))
    {
        if (chunk.frameCount == 0 || chunk.channelCount == 0)
        {
            continue;
        }

        consumedAny = true;
        const int oldChannelCount =
            static_cast<int>(state->document.getChannelCount());
        const int64_t oldFrameCount = state->document.getFrameCount();
        const int chunkChannelCount = static_cast<int>(chunk.channelCount);
        const int64_t requiredFrameCount =
            chunk.startFrame + static_cast<int64_t>(chunk.frameCount);

        if (state->document.getChannelCount() == 0)
        {
            state->document.initialize(cupuacu::SampleFormat::FLOAT32, 44100,
                                       chunk.channelCount, 0);
        }
        else if (state->document.getChannelCount() < chunkChannelCount)
        {
            state->document.resizeBuffer(
                chunkChannelCount, state->document.getFrameCount());

            for (int ch = oldChannelCount; ch < chunkChannelCount; ++ch)
            {
                auto &cache = state->document.getWaveformCache(ch);
                cache.clear();
                cache.applyInsert(0, state->document.getFrameCount());
                if (state->document.getFrameCount() > 0)
                {
                    cache.invalidateSamples(0, state->document.getFrameCount() - 1);
                }
            }
            waveformCacheChanged = true;
        }
        else if (requiredFrameCount > state->document.getFrameCount())
        {
            state->document.resizeBuffer(state->document.getChannelCount(),
                                         requiredFrameCount);
        }

        const int64_t appendCount =
            std::max<int64_t>(0, requiredFrameCount - oldFrameCount);
        if (appendCount > 0)
        {
            state->document.insertFrames(oldFrameCount, appendCount);
            waveformCacheChanged = true;
        }

        const int64_t overwriteStart =
            std::clamp<int64_t>(chunk.startFrame, 0, oldFrameCount);
        const int64_t overwriteEnd =
            std::min<int64_t>(requiredFrameCount, oldFrameCount) - 1;
        if (overwriteEnd >= overwriteStart)
        {
            for (int ch = 0; ch < state->document.getChannelCount(); ++ch)
            {
                state->document.getWaveformCache(ch).invalidateSamples(
                    overwriteStart, overwriteEnd);
            }
            waveformCacheChanged = true;
        }

        if (oldChannelCount != state->document.getChannelCount())
        {
            channelLayoutChanged = true;
        }

        for (uint32_t frame = 0; frame < chunk.frameCount; ++frame)
        {
            const int64_t writeFrame = chunk.startFrame + frame;
            const std::size_t base =
                static_cast<std::size_t>(frame) *
                cupuacu::audio::AudioDevices::kMaxRecordedChannels;

            state->document.setSample(0, writeFrame, chunk.interleavedSamples[base]);
            if (state->document.getChannelCount() > 1)
            {
                state->document.setSample(1, writeFrame,
                                          chunk.interleavedSamples[base + 1]);
            }
        }

        state->cursor = std::max(state->cursor, requiredFrameCount);
    }

    if (!consumedAny)
    {
        return false;
    }

    if (waveformCacheChanged)
    {
        state->document.updateWaveformCache();
    }

    if (channelLayoutChanged || static_cast<int>(state->waveforms.size()) !=
                                    state->document.getChannelCount())
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
    const auto borderWidth = computeBorderWidth();
    const float triHeight = borderWidth * 0.75f;
    const float halfBase = triHeight;

    const auto sampleOffset = state->sampleOffset;
    const auto samplesPerPixel = state->samplesPerPixel;

    if (state->selection.isActive())
    {
        cursorTop->setVisible(false);
        cursorBottom->setVisible(false);

        const int32_t startX = Waveform::getXPosForSampleIndex(
            state->selection.getStartInt(), sampleOffset, samplesPerPixel);
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

        const int64_t endInclusive = state->selection.getEndInt();
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
            state->cursor, sampleOffset, samplesPerPixel);
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
    const bool consumedRecordedAudio = consumePendingRecordedAudio();

    if (state->cursor != lastDrawnCursor ||
        state->selection.isActive() != lastSelectionIsActive ||
        state->samplesPerPixel != lastSamplesPerPixel ||
        state->sampleOffset != lastSampleOffset ||
        state->selection.getStartInt() != lastSelectionStart ||
        state->selection.getEndInt() != lastSelectionEnd ||
        consumedRecordedAudio)
    {
        lastDrawnCursor = state->cursor;
        lastSelectionIsActive = state->selection.isActive();
        lastSamplesPerPixel = state->samplesPerPixel;
        lastSampleOffset = state->sampleOffset;
        lastSelectionStart = state->selection.getStartInt();
        lastSelectionEnd = state->selection.getEndInt();

        updateTriangleMarkerBounds();
        setDirty();
    }
}
