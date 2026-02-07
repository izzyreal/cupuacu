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
    auto &session = state->activeDocumentSession;
    auto &doc = session.document;

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
        const int oldChannelCount = static_cast<int>(doc.getChannelCount());
        const int64_t oldFrameCount = doc.getFrameCount();
        const int chunkChannelCount = static_cast<int>(chunk.channelCount);
        const int64_t requiredFrameCount =
            chunk.startFrame + static_cast<int64_t>(chunk.frameCount);

        if (doc.getChannelCount() == 0)
        {
            doc.initialize(cupuacu::SampleFormat::FLOAT32, 44100,
                           chunk.channelCount, 0);
        }
        else if (doc.getChannelCount() < chunkChannelCount)
        {
            doc.resizeBuffer(chunkChannelCount, doc.getFrameCount());

            for (int ch = oldChannelCount; ch < chunkChannelCount; ++ch)
            {
                auto &cache = doc.getWaveformCache(ch);
                cache.clear();
                cache.applyInsert(0, doc.getFrameCount());
                if (doc.getFrameCount() > 0)
                {
                    cache.invalidateSamples(0, doc.getFrameCount() - 1);
                }
            }
            waveformCacheChanged = true;
        }
        else if (requiredFrameCount > doc.getFrameCount())
        {
            doc.resizeBuffer(doc.getChannelCount(), requiredFrameCount);
        }

        const int64_t appendCount =
            std::max<int64_t>(0, requiredFrameCount - oldFrameCount);
        if (appendCount > 0)
        {
            doc.insertFrames(oldFrameCount, appendCount);
            waveformCacheChanged = true;
        }

        const int64_t overwriteStart =
            std::clamp<int64_t>(chunk.startFrame, 0, oldFrameCount);
        const int64_t overwriteEnd =
            std::min<int64_t>(requiredFrameCount, oldFrameCount) - 1;
        if (overwriteEnd >= overwriteStart)
        {
            for (int ch = 0; ch < doc.getChannelCount(); ++ch)
            {
                doc.getWaveformCache(ch).invalidateSamples(overwriteStart,
                                                           overwriteEnd);
            }
            waveformCacheChanged = true;
        }

        if (oldChannelCount != doc.getChannelCount())
        {
            channelLayoutChanged = true;
        }

        for (uint32_t frame = 0; frame < chunk.frameCount; ++frame)
        {
            const int64_t writeFrame = chunk.startFrame + frame;
            const std::size_t base =
                static_cast<std::size_t>(frame) *
                cupuacu::audio::AudioDevices::kMaxRecordedChannels;

            doc.setSample(0, writeFrame, chunk.interleavedSamples[base]);
            if (doc.getChannelCount() > 1)
            {
                doc.setSample(1, writeFrame,
                              chunk.interleavedSamples[base + 1]);
            }
        }

        session.cursor = std::max(session.cursor, requiredFrameCount);
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
    const auto borderWidth = computeBorderWidth();
    const float triHeight = borderWidth * 0.75f;
    const float halfBase = triHeight;

    const auto sampleOffset = state->sampleOffset;
    const auto samplesPerPixel = state->samplesPerPixel;

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
    const bool consumedRecordedAudio = consumePendingRecordedAudio();

    if (session.cursor != lastDrawnCursor ||
        session.selection.isActive() != lastSelectionIsActive ||
        state->samplesPerPixel != lastSamplesPerPixel ||
        state->sampleOffset != lastSampleOffset ||
        session.selection.getStartInt() != lastSelectionStart ||
        session.selection.getEndInt() != lastSelectionEnd ||
        consumedRecordedAudio)
    {
        lastDrawnCursor = session.cursor;
        lastSelectionIsActive = session.selection.isActive();
        lastSamplesPerPixel = state->samplesPerPixel;
        lastSampleOffset = state->sampleOffset;
        lastSelectionStart = session.selection.getStartInt();
        lastSelectionEnd = session.selection.getEndInt();

        updateTriangleMarkerBounds();
        setDirty();
    }
}
