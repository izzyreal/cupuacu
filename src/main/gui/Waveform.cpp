#include "Waveform.h"

#include "WaveformsUnderlay.h"
#include "WaveformCache.h"

#include "smooth_line.h"

#include <limits>
#include <cmath>
#include <algorithm>

using namespace cupuacu::gui;

Waveform::Waveform(cupuacu::State *state, const uint8_t channelIndexToUse)
    : Component(state, "Waveform"), channelIndex(channelIndexToUse)
{
}

uint8_t Waveform::getChannelIndex() const
{
    return channelIndex;
}

uint16_t getSamplePointSize(const uint8_t pixelScale)
{
    return 32 / pixelScale;
}

void Waveform::resized()
{
    updateSamplePoints();
}

void Waveform::updateSamplePoints()
{
    removeAllChildren();

    if (shouldShowSamplePoints(state->samplesPerPixel, state->pixelScale))
    {
        auto samplePoints = computeSamplePoints();

        for (auto &sp : samplePoints)
        {
            addChild(sp);
        }
    }
}

bool Waveform::shouldShowSamplePoints(const double samplesPerPixel,
                                      const uint8_t pixelScale)
{
    return samplesPerPixel < ((float)pixelScale / 40.f);
}

int getYPosForSampleValue(const float sampleValue,
                          const uint16_t waveformHeight,
                          const double verticalZoom,
                          const uint16_t samplePointSize)
{
    const float drawableHeight = waveformHeight - samplePointSize;
    return (waveformHeight * 0.5f) - (sampleValue * verticalZoom * drawableHeight * 0.5f);
}

std::vector<std::unique_ptr<SamplePoint>> Waveform::computeSamplePoints()
{
    const double samplesPerPixel = state->samplesPerPixel;
    const int64_t sampleOffset = state->sampleOffset;
    const uint8_t pixelScale = state->pixelScale;
    const double verticalZoom = state->verticalZoom;
    const float halfSampleWidth = 0.5f / samplesPerPixel;

    const int64_t neededInputSamples = static_cast<int64_t>(std::round(getWidth() * samplesPerPixel));
    const int64_t availableSamples = state->document.getFrameCount() - sampleOffset;
    const int64_t actualInputSamples = std::min(neededInputSamples, availableSamples);

    if (actualInputSamples < 1)
    {
        return {};
    }

    std::vector<double> x(actualInputSamples);

    for (int i = 0; i < actualInputSamples; ++i)
    {
        x[i] = (i / samplesPerPixel) + halfSampleWidth;
    }

    std::vector<std::unique_ptr<SamplePoint>> result;
    const uint16_t samplePointSize = getSamplePointSize(pixelScale);

    auto sampleData = state->document.getAudioBuffer()->getImmutableChannelData(channelIndex);

    for (int i = 0; i < actualInputSamples; ++i)
    {
        const int xPos = x[i];
        const int yPos = getYPosForSampleValue(sampleData[sampleOffset + i], getHeight(), verticalZoom, samplePointSize);

        auto samplePoint = std::make_unique<SamplePoint>(state, channelIndex, sampleOffset + i);
        samplePoint->setBounds(std::max(0, xPos - (samplePointSize / 2)),
                               yPos - (samplePointSize / 2),
                               samplePointSize, samplePointSize);
        result.push_back(std::move(samplePoint));
    }

    return result;
}

void Waveform::drawHorizontalLines(SDL_Renderer* renderer)
{
    const auto samplePointSize = getSamplePointSize(state->pixelScale);
    const auto verticalZoom = state->verticalZoom;

    const uint16_t yCenter = getHeight() / 2; 

    SDL_SetRenderDrawColor(renderer, 100, 100, 100, 255);
    SDL_RenderLine(renderer, 0, yCenter, getWidth(), yCenter);

    if (verticalZoom <= 1.0)
    {
        SDL_SetRenderDrawColor(renderer, 150, 150, 150, 255);
        const int topY = samplePointSize / 2;
        const int bottomY = getHeight() - samplePointSize / 2;
        
        SDL_RenderLine(renderer, 0, topY, getWidth(), topY);
        SDL_RenderLine(renderer, 0, bottomY, getWidth(), bottomY);
    }
}

void Waveform::renderSmoothWaveform(SDL_Renderer* renderer)
{
    const auto samplesPerPixel = state->samplesPerPixel;
    const double halfSampleWidth = 0.5 / samplesPerPixel;
    const int64_t sampleOffset = state->sampleOffset;
    auto sampleData = state->document.getAudioBuffer()->getImmutableChannelData(channelIndex);
    const auto frameCount = state->document.getFrameCount();
    const auto verticalZoom = state->verticalZoom;
    const auto widthToUse = getWidth();
    const auto heightToUse = getHeight();
    const auto samplePointSize = getSamplePointSize(state->pixelScale);
    const auto drawableHeight = heightToUse - samplePointSize;

    const int64_t neededInputSamples = static_cast<int64_t>(std::ceil((widthToUse + 1) * samplesPerPixel));
    const int64_t availableSamples = frameCount - sampleOffset;
    const int64_t actualInputSamples = std::min(neededInputSamples, availableSamples);

    if (actualInputSamples < 1)
    {
        return;
    }

    std::vector<double> x(actualInputSamples);
    std::vector<double> y(actualInputSamples);

    for (int i = 0; i < actualInputSamples; ++i)
    {
        x[i] = (i / samplesPerPixel) + halfSampleWidth;
        y[i] = static_cast<double>(sampleData[sampleOffset + i]);
    }

    int numPoints = widthToUse + 1;
    std::vector<double> xq(numPoints);

    for (int i = 0; i < numPoints; ++i)
    {
        xq[i] = static_cast<double>(i);
    }

    auto smoothened = splineInterpolateNonUniform(x, y, xq);

    for (int i = 0; i < numPoints - 1; ++i)
    {
        float x1 = static_cast<float>(xq[i]);
        float x2 = static_cast<float>(xq[i + 1]);

        float y1f = heightToUse / 2.0f - (smoothened[i] * verticalZoom * drawableHeight / 2.0f);
        float y2f = heightToUse / 2.0f - (smoothened[i + 1] * verticalZoom * drawableHeight / 2.0f);

        float thickness = 1.0f;

        float dx = x2 - x1;
        float dy = y2f - y1f;
        float len = std::sqrt(dx*dx + dy*dy);
        if (len == 0.0f) continue;

        dx /= len; dy /= len;

        float px = -dy * thickness * 0.5f;
        float py = dx * thickness * 0.5f;

        SDL_Vertex verts[4];
        verts[0].position = { x1 - px, y1f - py };
        verts[1].position = { x1 + px, y1f + py };
        verts[2].position = { x2 + px, y2f + py };
        verts[3].position = { x2 - px, y2f - py };

        for (int j = 0; j < 4; ++j)
        {
            verts[j].color = waveformFColor;
            verts[j].tex_coord = { 0, 0 };
        }

        int indices[6] = { 0, 1, 2, 0, 2, 3 };
        SDL_RenderGeometry(renderer, nullptr, verts, 4, indices, 6);
    }
}

void Waveform::renderBlockWaveform(SDL_Renderer* renderer)
{
    SDL_SetRenderDrawColor(renderer, waveformColor.r, waveformColor.g, waveformColor.b, waveformColor.a);

    const double samplesPerPixel = state->samplesPerPixel;
    const int64_t sampleOffset = state->sampleOffset;
    const double verticalZoom = state->verticalZoom;
    const int widthToUse = getWidth();
    const int heightToUse = getHeight();
    const uint16_t samplePointSize = getSamplePointSize(state->pixelScale);
    const auto drawableHeight = heightToUse - samplePointSize;
    const float scale = (float)(verticalZoom * drawableHeight * 0.5f);
    const int centerY = heightToUse / 2;

    auto sampleData = state->document.getAudioBuffer()->getImmutableChannelData(channelIndex);
    const int64_t frameCount = state->document.getFrameCount();

    const bool bypassCache = samplesPerPixel < WaveformCache::BASE_BLOCK_SIZE;

    auto &waveformCache = state->document.getWaveformCache(channelIndex);

    if (!bypassCache && waveformCache.levelsCount() == 0)
        waveformCache.rebuildAll(sampleData.data(), frameCount);

    if (!bypassCache)
    {
        const auto& lvl0 = waveformCache.getLevel((double)WaveformCache::BASE_BLOCK_SIZE);
        if (lvl0.empty())
            waveformCache.rebuildAll(sampleData.data(), frameCount);
    }

    const int cacheLevel = bypassCache ? 0 : waveformCache.getLevelIndex(samplesPerPixel);
    const int64_t samplesPerPeak = bypassCache ? 0 : waveformCache.samplesPerPeakForLevel(cacheLevel);
    const auto& peaks = bypassCache ? *(const std::vector<Peak>*)nullptr : waveformCache.getLevel(samplesPerPixel);

    auto getPeakForPixel = [&](int x, Peak& out) -> bool
    {
        const double aD = (double)sampleOffset + (double)x * samplesPerPixel;
        const double bD = (double)sampleOffset + (double)(x + 1) * samplesPerPixel;

        if (bD <= 0.0) return false;
        if (aD >= (double)frameCount) return false;

        int64_t a = (int64_t)std::floor(aD);
        int64_t b = (int64_t)std::floor(bD);
        a = std::clamp<int64_t>(a, 0, frameCount - 1);
        b = std::clamp<int64_t>(b, a + 1, frameCount);

        if (bypassCache)
        {
            float minv = sampleData[a];
            float maxv = sampleData[a];
            for (int64_t i = a + 1; i < b; ++i)
            {
                float v = sampleData[i];
                minv = std::min(minv, v);
                maxv = std::max(maxv, v);
            }
            out = { minv, maxv };
            return true;
        }

        if (peaks.empty()) return false;

        const int64_t i0 = std::clamp<int64_t>(a / samplesPerPeak, 0, (int64_t)peaks.size() - 1);
        const int64_t i1 = std::clamp<int64_t>((b - 1) / samplesPerPeak, 0, (int64_t)peaks.size() - 1);

        float minv = peaks[i0].min;
        float maxv = peaks[i0].max;
        for (int64_t i = i0 + 1; i <= i1; ++i)
        {
            minv = std::min(minv, peaks[i].min);
            maxv = std::max(maxv, peaks[i].max);
        }

        out = { minv, maxv };
        return true;
    };

    int prevY = 0;
    bool hasPrev = false;

    for (int x = 0; x < widthToUse; ++x)
    {
        Peak p;
        if (!getPeakForPixel(x, p))
            continue;

        const int y1 = (int)(centerY - p.max * scale);
        const int y2 = (int)(centerY - p.min * scale);
        const int midY = (y1 + y2) / 2;

        if (y1 != y2)
            SDL_RenderLine(renderer, x, y1, x, y2);
        else
            SDL_RenderPoint(renderer, x, y1);

        if (hasPrev)
            SDL_RenderLine(renderer, x - 1, prevY, x, midY);

        prevY = midY;
        hasPrev = true;
    }
}

void Waveform::drawSelection(SDL_Renderer *renderer)
{
    const double samplesPerPixel = state->samplesPerPixel;

    const bool isSelected = state->selection.isActive() &&
        (state->selectedChannels == SelectedChannels::BOTH ||
        (channelIndex == 0 && state->selectedChannels == SelectedChannels::LEFT) ||
        (channelIndex == 1 && state->selectedChannels == SelectedChannels::RIGHT));

    const int64_t firstSample = state->selection.getStartInt();
    const int64_t lastSample = state->selection.getEndInt() + 1;
    const int64_t sampleOffset = state->sampleOffset;

    if (isSelected && lastSample >= sampleOffset)
    {
        const float startX = firstSample <= sampleOffset ? 0 : getXPosForSampleIndex(firstSample, sampleOffset, samplesPerPixel);
        const float endX = getXPosForSampleIndex(lastSample, sampleOffset, samplesPerPixel);

        SDL_SetRenderDrawColor(renderer, 0, 64, 255, 128);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        auto selectionWidth = std::abs(endX - startX) < 1 ? 1 : endX - startX;

        if (endX - startX < 0 && selectionWidth > 0) selectionWidth = -selectionWidth;

        SDL_FRect selectionRect = {
            startX,
            0.0f,
            selectionWidth,
            (float)getHeight()
        };

        SDL_RenderFillRect(renderer, &selectionRect);
    }
}

void Waveform::drawHighlight(SDL_Renderer *renderer)
{
    if (const auto waveformsUnderlay = dynamic_cast<WaveformsUnderlay*>(state->capturingComponent); waveformsUnderlay != nullptr)
    {
        return;
    }

    const auto samplesPerPixel = state->samplesPerPixel;

    if (shouldShowSamplePoints(samplesPerPixel, state->pixelScale) && samplePosUnderCursor.has_value())
    {
        const auto sampleOffset = state->sampleOffset;

        const auto samplePoint = dynamic_cast<SamplePoint*>(state->capturingComponent);
        const int64_t sampleIndex = samplePoint == nullptr ? *samplePosUnderCursor : samplePoint->getSampleIndex();

        if (sampleIndex < state->document.getFrameCount())
        {
            const float xPos = getXPosForSampleIndex(sampleIndex, sampleOffset, samplesPerPixel);
            const float sampleWidth = 1.0f / samplesPerPixel;

            SDL_SetRenderDrawColor(renderer, 0, 128, 255, 100);
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_FRect sampleRect = {
                xPos,
                0.0f,
                sampleWidth,
                (float)getHeight()
            };
            SDL_RenderFillRect(renderer, &sampleRect);
        }
    }
}

void Waveform::onDraw(SDL_Renderer *renderer)
{
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderFillRect(renderer, NULL);

    drawHorizontalLines(renderer);

    if (state->samplesPerPixel < 1)
    {
        renderSmoothWaveform(renderer);
    }
    else
    {
        renderBlockWaveform(renderer);
    }

    drawSelection(renderer);

    drawHighlight(renderer);

    drawCursor(renderer);

    drawPlaybackPosition(renderer);
}

void Waveform::drawPlaybackPosition(SDL_Renderer *renderer)
{
    const int64_t playbackPos = state->playbackPosition.load();

    if (playbackPos == -1)
    {
        return;
    }

    const int32_t playbackXPos = getXPosForSampleIndex(playbackPos, state->sampleOffset, state->samplesPerPixel);

    if (playbackXPos >= 0 && playbackXPos <= getWidth())
    {
        SDL_SetRenderDrawColor(renderer, 0, 200, 200, 255);
        SDL_RenderLine(renderer, playbackXPos, 0, playbackXPos, getHeight());
    }
}

void Waveform::drawCursor(SDL_Renderer *renderer)
{
    if (state->selection.isActive())
    {
        return;
    }

    const int32_t cursorXPos = getXPosForSampleIndex(state->cursor, state->sampleOffset, state->samplesPerPixel);

    if (cursorXPos >= 0 && cursorXPos <= getWidth())
    {
        SDL_SetRenderDrawColor(renderer, 188, 188, 0, 255);

        const int yInterval = 10 * (1.f/state->pixelScale);

        for (int i = yInterval; i < getHeight(); i += yInterval)
        {
            SDL_RenderPoint(renderer, cursorXPos, i);
        }
    }
}

void Waveform::timerCallback()
{
    if (samplePosUnderCursor.has_value() &&
        lastDrawnSamplePosUnderCursor != samplePosUnderCursor)
    {
        lastDrawnSamplePosUnderCursor = samplePosUnderCursor;
        setDirty();
    }

    if (const auto newPlaybackPosition = state->playbackPosition.load();
        newPlaybackPosition != playbackPosition)
    {
        playbackPosition = newPlaybackPosition;
        setDirty();
    }
}

void Waveform::mouseLeave()
{
    state->hoveringOverChannels = SelectedChannels::BOTH;

    for (auto &c : getChildren())
    {
        // Mouse just entered a sample point.
        // We don't want to clear the highlight in this case.
        if (state->componentUnderMouse == c.get())
        {
            return;
        }
    }

    clearHighlight();
}

void Waveform::clearHighlight()
{
    if (samplePosUnderCursor.has_value())
    {
        resetSamplePosUnderCursor();
        setDirty();
    }
}

void Waveform::setSamplePosUnderCursor(const int64_t samplePosUnderCursorToUse)
{
    if (samplePosUnderCursor.has_value() && *samplePosUnderCursor == samplePosUnderCursorToUse)
    {
        return;
    }

    samplePosUnderCursor.emplace(samplePosUnderCursorToUse);
    setDirty();
}

void Waveform::resetSamplePosUnderCursor()
{
    samplePosUnderCursor.reset();
    lastDrawnSamplePosUnderCursor = -1;
}

std::optional<int64_t> Waveform::getSamplePosUnderCursor() const
{
    return samplePosUnderCursor;
}

