#include "Waveform.h"

#include "WaveformsUnderlay.h"

#include "smooth_line.h"

#include <limits>
#include <cmath>
#include <algorithm>

Waveform::Waveform(CupuacuState *state, const uint8_t channelIndexToUse)
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
            addChildAndSetDirty(sp);
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

    const auto &sampleData = state->document.channels[channelIndex];

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
    const auto &sampleData = state->document.channels[channelIndex];
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

    const auto samplesPerPixel = state->samplesPerPixel;
    const int64_t sampleOffset = state->sampleOffset;
    const int64_t frameCount = state->document.getFrameCount();
    const auto verticalZoom = state->verticalZoom;
    const auto widthToUse = getWidth();
    const auto heightToUse = getHeight();
    const uint16_t samplePointSize = getSamplePointSize(state->pixelScale);
    const auto drawableHeight = heightToUse - samplePointSize;
    const auto halfSamplePointSize = samplePointSize / 2;

    const float scale = verticalZoom * drawableHeight * 0.5f;

    const uint64_t availableSamples = static_cast<uint64_t>(frameCount) - sampleOffset;
    const uint64_t actualInputSamples = std::min(static_cast<uint64_t>(std::ceil((widthToUse + 1) * samplesPerPixel)), availableSamples);
    const int maxPixel = static_cast<int>(std::ceil(actualInputSamples / samplesPerPixel));

    if (actualInputSamples < 4)
    {
        return;
    }

    int prevY = 0;
    bool hasPrev = false;

    const auto &sampleData = state->document.channels[channelIndex];

    for (int x = 0; x < std::min(static_cast<int64_t>(widthToUse), static_cast<int64_t>(maxPixel)); ++x)
    {
        const int64_t startSample = static_cast<int64_t>(x * samplesPerPixel) + static_cast<int64_t>(sampleOffset);
        int64_t endSample = std::min(static_cast<int64_t>((x + 1) * samplesPerPixel) + static_cast<int64_t>(sampleOffset), frameCount);

        if (startSample >= frameCount)
        {
            break;
        }

        if (endSample <= startSample)
        {
            endSample = startSample + 1;
        }

        float minSample = std::numeric_limits<float>::max();
        float maxSample = std::numeric_limits<float>::lowest();

        for (int64_t i = startSample; i < endSample; ++i)
        {
            const float s = sampleData[i];
            minSample = std::min(minSample, s);
            maxSample = std::max(maxSample, s);
        }

        const float midSample = (minSample + maxSample) * 0.5f;
        const int y = static_cast<int>(heightToUse / 2.0f - midSample * scale);

        if (hasPrev)
        {
            if ((y >= halfSamplePointSize || prevY >= halfSamplePointSize) &&
                (y < heightToUse - halfSamplePointSize || prevY < heightToUse - halfSamplePointSize))
            {
                SDL_RenderLine(renderer, x - 1, prevY, x, y);
            }
        }

        prevY = y;
        hasPrev = true;

        const int y1 = static_cast<int>(heightToUse / 2.0f - maxSample * scale);
        const int y2 = static_cast<int>(heightToUse / 2.0f - minSample * scale);

        if ((y1 < halfSamplePointSize && y2 < halfSamplePointSize) ||
            (y1 >= heightToUse - halfSamplePointSize && y2 >= heightToUse - halfSamplePointSize))
        {
            continue;
        }

        if (y1 != y2)
        {
            SDL_RenderLine(renderer, x, y1, x, y2);
        }
        else
        {
            SDL_RenderPoint(renderer, x, y1);
        }
    }
}

void Waveform::drawSelection(SDL_Renderer *renderer)
{
    const double samplesPerPixel = state->samplesPerPixel;

    const bool isSelected = state->selection.isActive() &&
        state->selectedChannels == SelectedChannels::BOTH ||
        (channelIndex == 0 && state->selectedChannels == SelectedChannels::LEFT) ||
        (channelIndex == 1 && state->selectedChannels == SelectedChannels::RIGHT);

    auto firstSample = static_cast<double>(state->selection.getStartInt());
    auto lastSample = static_cast<double>(state->selection.getEndInt() + 1);

    const int64_t sampleOffset = state->sampleOffset;

    if (isSelected && lastSample >= sampleOffset)
    {
        const float startX = firstSample <= sampleOffset ? 0 : (firstSample - sampleOffset) / samplesPerPixel;
        const float endX = (lastSample - sampleOffset) / samplesPerPixel;

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
            const float xPos = (sampleIndex - sampleOffset) / state->samplesPerPixel;
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

    const float samplesPerPixel = state->samplesPerPixel;

    if (samplesPerPixel < 1)
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
    if (state->playbackPosition.load() == -1)
    {
        return;
    }


    const int64_t sampleOffset = state->sampleOffset;
    const double samplesPerPixel = state->samplesPerPixel;

    const float lineX = (state->playbackPosition.load() - sampleOffset) / samplesPerPixel;

    if (lineX >= 0 && lineX <= getWidth())
    {
        SDL_SetRenderDrawColor(renderer, 0, 200, 200, 255);
        SDL_RenderLine(renderer, (int)lineX, 0, (int)lineX, getHeight());
    }
}

void Waveform::drawCursor(SDL_Renderer *renderer)
{
    if (state->selection.isActive())
    {
        return;
    }

    const int64_t sampleOffset = state->sampleOffset;
    const double samplesPerPixel = state->samplesPerPixel;

    const float lineX = (state->cursor - sampleOffset) / samplesPerPixel;

    if (lineX >= 0 && lineX <= getWidth())
    {
        SDL_SetRenderDrawColor(renderer, 188, 188, 0, 255);

        const int yInterval = 10 * (1.f/state->pixelScale);

        for (int i = yInterval; i < getHeight(); i += yInterval)
        {
            SDL_RenderPoint(renderer, lineX, i);
        }
    }
}

void Waveform::timerCallback()
{
    if (samplePosUnderCursor.has_value() &&
        lastDrawnSamplePosUnderCursor != samplePosUnderCursor)
    {
        lastDrawnSamplePosUnderCursor = samplePosUnderCursor;
        setDirtyRecursive();
    }

    if (const auto newPlaybackPosition = state->playbackPosition.load();
        newPlaybackPosition != playbackPosition)
    {
        playbackPosition = newPlaybackPosition;
        setDirtyRecursive();
    }
}

void Waveform::mouseLeave()
{
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
        setDirtyRecursive();
    }
}

void Waveform::setSamplePosUnderCursor(const int64_t samplePosUnderCursorToUse)
{
    if (samplePosUnderCursor.has_value() && *samplePosUnderCursor == samplePosUnderCursorToUse)
    {
        return;
    }

    samplePosUnderCursor.emplace(samplePosUnderCursorToUse);
    setDirtyRecursive();
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

