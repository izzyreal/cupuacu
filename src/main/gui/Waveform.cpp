#include "Waveform.h"

#include "WaveformsOverlay.h"

#include <limits>
#include <cmath>
#include <algorithm>
#include "smooth_line.h"

Waveform::Waveform(CupuacuState *state, const uint8_t channelIndexToUse)
    : Component(state, "Waveform"), channelIndex(channelIndexToUse), samplePosUnderCursor(-1)
{
}

int getSamplePointSize(const int hardwarePixelsPerAppPixel)
{
    return 32 / hardwarePixelsPerAppPixel;
}

void Waveform::updateSamplePoints()
{
    removeAllChildren();

    if (shouldShowSamplePoints(state->samplesPerPixel, state->hardwarePixelsPerAppPixel))
    {
        auto samplePoints = computeSamplePoints();

        for (auto &sp : samplePoints)
        {
            addChildAndSetDirty(sp);
        }
    }
}

bool Waveform::shouldShowSamplePoints(const double samplesPerPixel,
                                      const uint8_t hardwarePixelsPerAppPixel)
{
    return samplesPerPixel < ((float)hardwarePixelsPerAppPixel / 40.f);
}

int getYPosForSampleValue(const float sampleValue,
                          const uint16_t waveformHeight,
                          const double verticalZoom,
                          const int samplePointSize)
{
    const float drawableHeight = waveformHeight - samplePointSize;
    return (waveformHeight * 0.5f) - (sampleValue * verticalZoom * drawableHeight * 0.5f);
}

std::vector<std::unique_ptr<SamplePoint>> Waveform::computeSamplePoints()
{
    const auto samplesPerPixel = state->samplesPerPixel;
    const auto sampleOffset = state->sampleOffset;
    const auto &sampleData = state->document.channels[channelIndex];
    const auto hardwarePixelsPerAppPixel = state->hardwarePixelsPerAppPixel;
    const auto verticalZoom = state->verticalZoom;

    const int neededInputSamples = static_cast<int>(std::ceil((getWidth() + 1) * samplesPerPixel));
    const int availableSamples = static_cast<int>(sampleData.size()) - static_cast<int>(sampleOffset);
    const int actualInputSamples = std::min(neededInputSamples, availableSamples);

    if (actualInputSamples < 4)
    {
        return {};
    }

    std::vector<double> x(actualInputSamples);

    for (int i = 0; i < actualInputSamples; ++i)
    {
        x[i] = i / samplesPerPixel;
    }

    std::vector<std::unique_ptr<SamplePoint>> result;
    const auto samplePointSize = getSamplePointSize(hardwarePixelsPerAppPixel);

    for (int i = 0; i < actualInputSamples; ++i)
    {
        const int xPos = x[i];
        const int yPos = getYPosForSampleValue(sampleData[sampleOffset + i], getHeight(), verticalZoom, samplePointSize);

        auto samplePoint = std::make_unique<SamplePoint>(state, channelIndex, sampleOffset + i);
        samplePoint->setBounds(std::max(0, xPos - (samplePointSize / 2)),
                               yPos - (samplePointSize / 2),
                               i == 0 ? samplePointSize / 2 : samplePointSize, samplePointSize);
        result.push_back(std::move(samplePoint));
    }

    return result;
}

void Waveform::drawHorizontalLines(SDL_Renderer* renderer)
{
    const auto heightToUse = getHeight();
    const auto samplePointSize = getSamplePointSize(state->hardwarePixelsPerAppPixel);
    const auto verticalZoom = state->verticalZoom;

    // Draw DC line (center line)
    SDL_SetRenderDrawColor(renderer, 100, 100, 100, 255); // Gray color for DC line
    SDL_RenderLine(renderer, 0, heightToUse / 2, getWidth(), heightToUse / 2);

    // Draw boundary lines only if vertical zoom <= 1.0
    if (verticalZoom <= 1.0)
    {
        SDL_SetRenderDrawColor(renderer, 150, 150, 150, 255); // Light gray for boundaries
        const int topY = samplePointSize / 2;
        const int bottomY = heightToUse - samplePointSize / 2;
        
        // Top boundary
        SDL_RenderLine(renderer, 0, topY, getWidth(), topY);
        // Bottom boundary
        SDL_RenderLine(renderer, 0, bottomY, getWidth(), bottomY);
    }
}

void Waveform::renderSmoothWaveform(SDL_Renderer* renderer)
{
    SDL_SetRenderDrawColor(renderer, 0, 185, 0, 255);

    const auto samplesPerPixel = state->samplesPerPixel;
    const auto sampleOffset = state->sampleOffset;
    const auto &sampleData = state->document.channels[channelIndex];
    const auto verticalZoom = state->verticalZoom;
    const auto widthToUse = getWidth();
    const auto heightToUse = getHeight();
    const auto samplePointSize = getSamplePointSize(state->hardwarePixelsPerAppPixel);
    const auto drawableHeight = heightToUse - samplePointSize;

    const int neededInputSamples = static_cast<int>(std::ceil((widthToUse + 1) * samplesPerPixel));
    const int availableSamples = static_cast<int>(sampleData.size()) - static_cast<int>(sampleOffset);
    const int actualInputSamples = std::min(neededInputSamples, availableSamples);

    if (actualInputSamples < 4)
    {
        return;
    }

    std::vector<double> x(actualInputSamples);
    std::vector<double> y(actualInputSamples);

    for (int i = 0; i < actualInputSamples; ++i)
    {
        x[i] = i / samplesPerPixel;
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
            verts[j].color = { 0, 0.5, 0, 1.f };
            verts[j].tex_coord = { 0, 0 };
        }

        int indices[6] = { 0, 1, 2, 0, 2, 3 };
        SDL_RenderGeometry(renderer, nullptr, verts, 4, indices, 6);
    }
}

void Waveform::renderBlockWaveform(SDL_Renderer* renderer)
{
    SDL_SetRenderDrawColor(renderer, 0, 185, 0, 255);

    const auto samplesPerPixel = state->samplesPerPixel;
    const auto sampleOffset = state->sampleOffset;
    const auto &sampleData = state->document.channels[channelIndex];
    const auto verticalZoom = state->verticalZoom;
    const auto widthToUse = getWidth();
    const auto heightToUse = getHeight();
    const auto samplePointSize = getSamplePointSize(state->hardwarePixelsPerAppPixel);
    const auto drawableHeight = heightToUse - samplePointSize;

    const float scale = verticalZoom * drawableHeight * 0.5f;

    int prevY = 0;
    bool hasPrev = false;
    bool noMoreStuffToDraw = false;

    for (int x = 0; x < widthToUse; ++x)
    {
        if (noMoreStuffToDraw)
        {
            break;
        }

        const size_t startSample = static_cast<size_t>(x * samplesPerPixel) + sampleOffset;
        size_t endSample = static_cast<size_t>((x + 1) * samplesPerPixel) + sampleOffset;

        if (endSample == startSample)
        {
            endSample++;
        }

        float minSample = std::numeric_limits<float>::max();
        float maxSample = std::numeric_limits<float>::lowest();

        for (size_t i = startSample; i < endSample; ++i)
        {
            if (i >= sampleData.size())
            {
                noMoreStuffToDraw = true;
                break;
            }

            const float s = sampleData[i];
            minSample = std::min(minSample, s);
            maxSample = std::max(maxSample, s);
        }

        const float midSample = (minSample + maxSample) * 0.5f;
        const int y = static_cast<int>(float(heightToUse) / 2 - midSample * scale);

        if (hasPrev)
        {
            if ((y >= samplePointSize / 2 || prevY >= samplePointSize / 2) && 
                (y < heightToUse - samplePointSize / 2 || prevY < heightToUse - samplePointSize / 2))
            {
                SDL_RenderLine(renderer, x - 1, prevY, x, y);
            }
        }

        if (startSample >= sampleData.size())
        {
            break;
        }

        prevY = y;
        hasPrev = true;

        int y1 = static_cast<int>(float(heightToUse) / 2 - maxSample * scale);
        int y2 = static_cast<int>(float(heightToUse) / 2 - minSample * scale);

        if ((y1 < samplePointSize / 2 && y2 < samplePointSize / 2) || 
            (y1 >= heightToUse - samplePointSize / 2 && y2 >= heightToUse - samplePointSize / 2))
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

    // Draw rectangle over sample point under mouse when sample points are visible
    if (shouldShowSamplePoints(samplesPerPixel, state->hardwarePixelsPerAppPixel))
    {
        const auto mouseX = state->mouseX;
        const auto sampleOffset = state->sampleOffset;
        const auto& sampleData = state->document.channels[channelIndex];

        const auto *samplePoint = dynamic_cast<SamplePoint*>(state->capturingComponent);

        if ((mouseX >= 0 && mouseX < getWidth() && !sampleData.empty()) ||
            samplePoint != nullptr)
        {
            const size_t sampleIndex = samplePoint != nullptr ? samplePoint->getSampleIndex() : static_cast<size_t>(
                Waveform::xPositionToSampleIndex(mouseX, sampleOffset, samplesPerPixel, true));
            if (sampleIndex < sampleData.size())
            {
                // Check if mouse has moved to a new sample region
                if (sampleIndex != static_cast<size_t>(samplePosUnderCursor))
                {
                    setDirtyRecursive();
                    samplePosUnderCursor = static_cast<int64_t>(sampleIndex);
                }

                const float xPos = Waveform::sampleIndexToXPosition(
                    static_cast<float>(sampleIndex), sampleOffset, samplesPerPixel, true);
                const float sampleWidth = 1.0f / samplesPerPixel;

                SDL_SetRenderDrawColor(renderer, 0, 128, 255, 100); // Light blue, semi-transparent
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
                SDL_FRect sampleRect = {
                    xPos,
                    0.0f,
                    sampleWidth,
                    (float)getHeight()
                };
                SDL_RenderFillRect(renderer, &sampleRect);
            }
            else
            {
                // Clear samplePosUnderCursor if out of bounds
                if (samplePosUnderCursor != -1)
                {
                    samplePosUnderCursor = -1;
                    setDirtyRecursive();
                }
            }
        }
        else
        {
            // Clear samplePosUnderCursor if mouse is outside waveform
            if (samplePosUnderCursor != -1)
            {
                samplePosUnderCursor = -1;
                setDirtyRecursive();
            }
        }
    }

    const auto isSelected = state->selection.isActive() &&
                            channelIndex >= state->selectionChannelStart &&
                            channelIndex <= state->selectionChannelEnd;

    auto firstSample = state->selection.getStartFloor();
    auto lastSample = state->selection.getEndFloor();

    const size_t sampleOffset = state->sampleOffset;

    if (isSelected && lastSample >= sampleOffset)
    {
        if (shouldShowSamplePoints(samplesPerPixel, state->hardwarePixelsPerAppPixel))
        {
            firstSample -= 0.5f;
            lastSample -= 0.5f;
        }

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

    drawPlaybackPosition(renderer, sampleOffset, samplesPerPixel);
}

void Waveform::drawPlaybackPosition(SDL_Renderer *renderer,
                                    const double sampleOffset,
                                    const double samplesPerPixel)
{
    const float lineX = (state->playbackPosition.load() - sampleOffset) / samplesPerPixel;

    if (lineX >= 0 && lineX <= getWidth())
    {
        SDL_SetRenderDrawColor(renderer, 0, 200, 200, 255);
        SDL_RenderLine(renderer, (int)lineX, 0, (int)lineX, getHeight());
    }
}

void Waveform::timerCallback()
{
    // Check for mouse movement to a new sample region
    if (shouldShowSamplePoints(state->samplesPerPixel, state->hardwarePixelsPerAppPixel))
    {
        if (const auto *samplePoint = dynamic_cast<SamplePoint*>(state->capturingComponent); samplePoint != nullptr)
        {
            return;
        }

        const auto mouseX = state->mouseX;
        const auto sampleOffset = state->sampleOffset;
        const auto& sampleData = state->document.channels[channelIndex];

        if (mouseX >= 0 && mouseX < getWidth() && !sampleData.empty())
        {
            const size_t sampleIndex = static_cast<size_t>(
                Waveform::xPositionToSampleIndex(mouseX, sampleOffset, state->samplesPerPixel, true));
            if (sampleIndex < sampleData.size())
            {
                if (sampleIndex != static_cast<size_t>(samplePosUnderCursor))
                {
                    samplePosUnderCursor = static_cast<int64_t>(sampleIndex);
                    setDirtyRecursive();
                }
            }
            else
            {
                if (samplePosUnderCursor != -1)
                {
                    samplePosUnderCursor = -1;
                    setDirtyRecursive();
                }
            }
        }
        else
        {
            if (samplePosUnderCursor != -1)
            {
                samplePosUnderCursor = -1;
                setDirtyRecursive();
            }
        }
    }
    else
    {
        if (samplePosUnderCursor != -1)
        {
            samplePosUnderCursor = -1;
            setDirtyRecursive();
        }
    }

    if (state->samplesToScroll != 0.0f)
    {
        const auto scroll = state->samplesToScroll;
        const uint64_t oldOffset = state->sampleOffset;

        if (scroll < 0)
        {
            auto absScroll = -scroll;
            state->sampleOffset = (state->sampleOffset > absScroll)
                ? state->sampleOffset - absScroll
                : 0;
        }
        else
        {
            state->sampleOffset += scroll;
        }

        if (oldOffset != state->sampleOffset)
        {
            state->componentUnderMouse = nullptr;
            setDirty();
            updateSamplePoints();
        }
    }

    if (const auto newPlaybackPosition = state->playbackPosition.load();
        newPlaybackPosition != playbackPosition)
    {
        playbackPosition = newPlaybackPosition;
        setDirtyRecursive();
    }
}
