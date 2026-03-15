#include "Waveform.hpp"

#include "audio/AudioDevices.hpp"
#include "WaveformBlockRenderPlanning.hpp"
#include "WaveformSamplePointPlanning.hpp"
#include "WaveformSmoothRenderPlanning.hpp"
#include "WaveformsUnderlay.hpp"
#include "WaveformCache.hpp"
#include "WaveformVisualState.hpp"
#include "Window.hpp"

#include "smooth_line.hpp"

#include <limits>
#include <cmath>
#include <algorithm>

using namespace cupuacu::gui;

Waveform::Waveform(State *state, const uint8_t channelIndexToUse)
    : Component(state, "Waveform"), channelIndex(channelIndexToUse)
{
}

Waveform::~Waveform()
{
    invalidateBaseTexture();
}

uint8_t Waveform::getChannelIndex() const
{
    return channelIndex;
}

void Waveform::resized()
{
    invalidateBaseTexture();
    updateSamplePoints();
}

void Waveform::invalidateBaseTexture() const
{
    cachedBaseTextureValid = false;
    if (cachedBaseTexture)
    {
        SDL_DestroyTexture(cachedBaseTexture);
        cachedBaseTexture = nullptr;
    }
}

Waveform::BaseTextureCacheKey Waveform::computeBaseTextureCacheKey() const
{
    const auto &viewState = state->mainDocumentSessionWindow->getViewState();
    const auto &doc = state->activeDocumentSession.document;
    return {
        .width = getWidth(),
        .height = getHeight(),
        .viewWidth = getWidth(),
        .viewHeight = getHeight(),
        .sampleOffset = viewState.sampleOffset,
        .frameCount = doc.getFrameCount(),
        .samplesPerPixel = viewState.samplesPerPixel,
        .verticalZoom = viewState.verticalZoom,
        .waveformDataVersion = doc.getWaveformDataVersion(),
        .pixelScale = state->pixelScale,
    };
}

Waveform::BaseTextureCacheKey Waveform::makeCurrentBlockTextureCoverageKey(
    const BaseTextureCacheKey &currentViewKey) const
{
    const int coverageWidth =
        std::max(currentViewKey.viewWidth * 2, currentViewKey.viewWidth + 64);
    const int extraWidth = coverageWidth - currentViewKey.viewWidth;
    const double extraSamples =
        currentViewKey.samplesPerPixel * static_cast<double>(extraWidth) * 0.5;
    const int64_t coverageSpanSamples = static_cast<int64_t>(
        std::ceil(currentViewKey.samplesPerPixel * coverageWidth));
    const int64_t maxCoverageOffset = std::max<int64_t>(
        0, currentViewKey.frameCount - coverageSpanSamples);
    const int64_t coverageOffset = std::clamp<int64_t>(
        currentViewKey.sampleOffset -
            static_cast<int64_t>(std::llround(extraSamples)),
        0, maxCoverageOffset);

    auto coverageKey = currentViewKey;
    coverageKey.width = coverageWidth;
    coverageKey.height = currentViewKey.viewHeight;
    coverageKey.sampleOffset = coverageOffset;
    return coverageKey;
}

bool Waveform::canRenderCurrentViewFromCachedBlockTexture(
    const BaseTextureCacheKey &currentViewKey) const
{
    if (!cachedBaseTextureValid || !cachedBaseTexture ||
        currentViewKey.samplesPerPixel < 1.0)
    {
        return false;
    }

    if (cachedBaseTextureKey.viewWidth != currentViewKey.viewWidth ||
        cachedBaseTextureKey.viewHeight != currentViewKey.viewHeight ||
        cachedBaseTextureKey.height != currentViewKey.viewHeight ||
        cachedBaseTextureKey.frameCount != currentViewKey.frameCount ||
        cachedBaseTextureKey.waveformDataVersion !=
            currentViewKey.waveformDataVersion ||
        cachedBaseTextureKey.pixelScale != currentViewKey.pixelScale ||
        cachedBaseTextureKey.samplesPerPixel != currentViewKey.samplesPerPixel ||
        cachedBaseTextureKey.verticalZoom != currentViewKey.verticalZoom)
    {
        return false;
    }

    const double sourceX =
        (static_cast<double>(currentViewKey.sampleOffset) -
         static_cast<double>(cachedBaseTextureKey.sampleOffset)) /
        currentViewKey.samplesPerPixel;
    if (sourceX < 0.0)
    {
        return false;
    }

    if (sourceX + static_cast<double>(currentViewKey.viewWidth) >
        static_cast<double>(cachedBaseTextureKey.width))
    {
        return false;
    }

    cachedBaseTextureSourceRect = {static_cast<float>(sourceX), 0.0f,
                                   static_cast<float>(currentViewKey.viewWidth),
                                   static_cast<float>(currentViewKey.viewHeight)};
    return true;
}

void Waveform::drawBaseWaveformContents(SDL_Renderer *renderer) const
{
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderFillRect(renderer, NULL);

    drawHorizontalLines(renderer);

    const auto &viewState = state->mainDocumentSessionWindow->getViewState();
    if (viewState.samplesPerPixel < 1)
    {
        renderSmoothWaveform(renderer);
    }
    else
    {
        renderBlockWaveform(renderer);
    }
}

bool Waveform::ensureBaseTexture(SDL_Renderer *renderer) const
{
    if (!renderer || getWidth() <= 0 || getHeight() <= 0)
    {
        return false;
    }

    const auto newKey = computeBaseTextureCacheKey();
    const bool isBlockMode = newKey.samplesPerPixel >= 1.0;
    if (isBlockMode && canRenderCurrentViewFromCachedBlockTexture(newKey))
    {
        return true;
    }

    if (cachedBaseTextureValid && cachedBaseTexture &&
        cachedBaseTextureKey == newKey)
    {
        cachedBaseTextureSourceRect = {0.0f, 0.0f, static_cast<float>(newKey.width),
                                       static_cast<float>(newKey.height)};
        return true;
    }

    int pixelShift = 0;
    if (!isBlockMode && cachedBaseTextureValid && cachedBaseTexture &&
        canReuseBlockTextureForHorizontalShift(newKey, pixelShift) &&
        rebuildShiftedBlockTexture(renderer, newKey, pixelShift))
    {
        cachedBaseTextureKey = newKey;
        cachedBaseTextureValid = true;
        cachedBaseTextureSourceRect = {0.0f, 0.0f, static_cast<float>(newKey.width),
                                       static_cast<float>(newKey.height)};
        return true;
    }

    const auto targetKey =
        isBlockMode ? makeCurrentBlockTextureCoverageKey(newKey) : newKey;

    if (!cachedBaseTexture ||
        cachedBaseTextureKey.width != targetKey.width ||
        cachedBaseTextureKey.height != targetKey.height)
    {
        if (cachedBaseTexture)
        {
            SDL_DestroyTexture(cachedBaseTexture);
            cachedBaseTexture = nullptr;
        }

        cachedBaseTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888,
                                              SDL_TEXTUREACCESS_TARGET,
                                              targetKey.width, targetKey.height);
        if (!cachedBaseTexture)
        {
            cachedBaseTextureValid = false;
            return false;
        }
        SDL_SetTextureScaleMode(cachedBaseTexture, SDL_SCALEMODE_NEAREST);
    }

    SDL_Texture *previousTarget = SDL_GetRenderTarget(renderer);
    SDL_Rect previousViewport{};
    SDL_GetRenderViewport(renderer, &previousViewport);

    SDL_SetRenderTarget(renderer, cachedBaseTexture);
    const SDL_Rect localViewport{0, 0, targetKey.width, targetKey.height};
    SDL_SetRenderViewport(renderer, &localViewport);
    if (isBlockMode)
    {
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderFillRect(renderer, nullptr);
        drawHorizontalLines(renderer);
        renderBlockWaveformRange(renderer, 0, targetKey.width, targetKey.width,
                                 targetKey.sampleOffset);
    }
    else
    {
        drawBaseWaveformContents(renderer);
    }

    SDL_SetRenderTarget(renderer, previousTarget);
    SDL_SetRenderViewport(renderer, &previousViewport);

    cachedBaseTextureKey = targetKey;
    cachedBaseTextureValid = true;
    if (isBlockMode)
    {
        canRenderCurrentViewFromCachedBlockTexture(newKey);
    }
    else
    {
        cachedBaseTextureSourceRect = {
            0.0f, 0.0f, static_cast<float>(newKey.width),
            static_cast<float>(newKey.height)};
    }
    return true;
}

bool Waveform::canReuseBlockTextureForHorizontalShift(
    const BaseTextureCacheKey &newKey, int &outPixelShift) const
{
    outPixelShift = 0;
    if (!cachedBaseTextureValid)
    {
        return false;
    }

    if (cachedBaseTextureKey.width != newKey.width ||
        cachedBaseTextureKey.height != newKey.height ||
        cachedBaseTextureKey.frameCount != newKey.frameCount ||
        cachedBaseTextureKey.waveformDataVersion != newKey.waveformDataVersion ||
        cachedBaseTextureKey.pixelScale != newKey.pixelScale)
    {
        return false;
    }

    if (cachedBaseTextureKey.samplesPerPixel != newKey.samplesPerPixel ||
        cachedBaseTextureKey.verticalZoom != newKey.verticalZoom)
    {
        return false;
    }

    if (newKey.samplesPerPixel < 1.0)
    {
        return false;
    }

    const int64_t deltaSamples =
        newKey.sampleOffset - cachedBaseTextureKey.sampleOffset;
    if (deltaSamples == 0)
    {
        return false;
    }

    const double pixelShiftD =
        -static_cast<double>(deltaSamples) / newKey.samplesPerPixel;
    const int pixelShift = static_cast<int>(std::lround(pixelShiftD));
    if (pixelShift == 0 || std::abs(pixelShift) >= newKey.width)
    {
        return false;
    }
    if (std::abs(pixelShiftD - static_cast<double>(pixelShift)) > 1e-6)
    {
        return false;
    }

    outPixelShift = pixelShift;
    return true;
}

bool Waveform::rebuildShiftedBlockTexture(SDL_Renderer *renderer,
                                          const BaseTextureCacheKey &newKey,
                                          int pixelShift) const
{
    if (!cachedBaseTexture)
    {
        return false;
    }

    SDL_Texture *shiftedTexture = SDL_CreateTexture(
        renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET,
        newKey.width, newKey.height);
    if (!shiftedTexture)
    {
        return false;
    }
    SDL_SetTextureScaleMode(shiftedTexture, SDL_SCALEMODE_NEAREST);

    SDL_Texture *previousTarget = SDL_GetRenderTarget(renderer);
    SDL_Rect previousViewport{};
    SDL_GetRenderViewport(renderer, &previousViewport);

    SDL_SetRenderTarget(renderer, shiftedTexture);
    const SDL_Rect fullViewport{0, 0, newKey.width, newKey.height};
    SDL_SetRenderViewport(renderer, &fullViewport);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderFillRect(renderer, nullptr);

    SDL_FRect shiftedDest{static_cast<float>(pixelShift), 0.0f,
                          static_cast<float>(newKey.width),
                          static_cast<float>(newKey.height)};
    SDL_RenderTexture(renderer, cachedBaseTexture, nullptr, &shiftedDest);

    SDL_Rect exposedRect{};
    if (pixelShift > 0)
    {
        exposedRect = {0, 0, pixelShift, newKey.height};
    }
    else
    {
        exposedRect = {newKey.width + pixelShift, 0, -pixelShift, newKey.height};
    }

    if (exposedRect.w > 0 && exposedRect.h > 0)
    {
        SDL_SetRenderViewport(renderer, &exposedRect);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderFillRect(renderer, nullptr);
        drawHorizontalLines(renderer);
        renderBlockWaveformRange(renderer, exposedRect.x,
                                 exposedRect.x + exposedRect.w, newKey.width,
                                 newKey.sampleOffset);
    }

    SDL_SetRenderTarget(renderer, previousTarget);
    SDL_SetRenderViewport(renderer, &previousViewport);

    SDL_DestroyTexture(cachedBaseTexture);
    cachedBaseTexture = shiftedTexture;
    return true;
}

void Waveform::updateSamplePoints()
{
    removeAllChildren();
    const auto &viewState = state->mainDocumentSessionWindow->getViewState();
    if (shouldRenderWaveformSamplePoints(playbackPosition,
                                         viewState.samplesPerPixel,
                                         state->pixelScale))
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
    return shouldRenderWaveformSamplePoints(-1, samplesPerPixel, pixelScale);
}

bool Waveform::computeBlockModeSelectionRect(
    const int64_t firstSample, const int64_t lastSampleExclusive,
    const int64_t sampleOffset, const double samplesPerPixel, const int width,
    const int height, SDL_FRect &outRect,
    const int64_t samplesPerPeakForDisplay,
    const bool includeConnectorPixelPadding)
{
    if (samplesPerPixel <= 0.0 || width <= 0 || height <= 0 ||
        lastSampleExclusive <= firstSample)
    {
        return false;
    }

    int64_t mappedFirst = firstSample;
    int64_t mappedLastExclusive = lastSampleExclusive;
    if (samplesPerPeakForDisplay > 1)
    {
        const int64_t q = samplesPerPeakForDisplay;
        mappedFirst = (mappedFirst / q) * q;
        mappedLastExclusive = ((mappedLastExclusive + q - 1) / q) * q;
    }

    const double startPxD =
        (static_cast<double>(mappedFirst) - static_cast<double>(sampleOffset)) /
        samplesPerPixel;
    const double endPxD =
        (static_cast<double>(mappedLastExclusive) -
         static_cast<double>(sampleOffset)) /
        samplesPerPixel;

    const int startPx = static_cast<int>(std::floor(startPxD));
    const int endPx = static_cast<int>(std::ceil(endPxD));

    if (endPx <= 0 || startPx >= width)
    {
        return false;
    }

    int drawStart = std::clamp(startPx, 0, width);
    int drawEnd = std::clamp(endPx, 0, width);

    if (includeConnectorPixelPadding)
    {
        drawStart = std::max(0, drawStart - 1);
        drawEnd = std::min(width, drawEnd + 1);
    }

    if (drawEnd < drawStart)
    {
        return false;
    }

    const int drawWidth = std::max(1, drawEnd - drawStart);
    outRect = {static_cast<float>(drawStart), 0.0f,
               static_cast<float>(drawWidth), static_cast<float>(height)};
    return true;
}

bool Waveform::computeBlockModeSelectionEdgePixels(
    const int64_t firstSample, const int64_t lastSampleExclusive,
    const int64_t sampleOffset, const double samplesPerPixel, const int width,
    int32_t &outStartEdgePx, int32_t &outEndEdgePxExclusive,
    const int64_t samplesPerPeakForDisplay,
    const bool includeConnectorPixelPadding)
{
    SDL_FRect rect{};
    if (!computeBlockModeSelectionRect(firstSample, lastSampleExclusive,
                                       sampleOffset, samplesPerPixel, width, 1,
                                       rect, samplesPerPeakForDisplay,
                                       includeConnectorPixelPadding))
    {
        return false;
    }

    outStartEdgePx = static_cast<int32_t>(std::lround(rect.x));
    outEndEdgePxExclusive =
        static_cast<int32_t>(std::lround(rect.x + rect.w));
    return true;
}

int getYPosForSampleValue(const float sampleValue,
                          const uint16_t waveformHeight,
                          const double verticalZoom,
                          const uint16_t samplePointSize)
{
    const float drawableHeight = waveformHeight - samplePointSize;
    return waveformHeight * 0.5f -
           sampleValue * verticalZoom * drawableHeight * 0.5f;
}

std::vector<std::unique_ptr<SamplePoint>> Waveform::computeSamplePoints()
{
    const auto &session = state->activeDocumentSession;
    const auto &doc = session.document;
    const auto &viewState = state->mainDocumentSessionWindow->getViewState();
    const auto sampleData =
        doc.getAudioBuffer()->getImmutableChannelData(channelIndex);
    const auto plannedPoints = planWaveformSamplePoints(
        getWidth(), getHeight(), viewState.samplesPerPixel,
        viewState.sampleOffset, state->pixelScale, viewState.verticalZoom,
        doc.getFrameCount(),
        [&](const int64_t sampleIndex)
        {
            return sampleData[static_cast<std::size_t>(sampleIndex)];
        });

    std::vector<std::unique_ptr<SamplePoint>> result;
    result.reserve(plannedPoints.size());

    for (const auto &plannedPoint : plannedPoints)
    {
        auto samplePoint = std::make_unique<SamplePoint>(
            state, channelIndex, plannedPoint.sampleIndex);
        samplePoint->setBounds(plannedPoint.x, plannedPoint.y,
                               plannedPoint.size, plannedPoint.size);
        result.push_back(std::move(samplePoint));
    }

    return result;
}

void Waveform::drawHorizontalLines(SDL_Renderer *renderer) const
{
    const auto samplePointSize = getWaveformSamplePointSize(state->pixelScale);
    const auto &viewState = state->mainDocumentSessionWindow->getViewState();
    const auto verticalZoom = viewState.verticalZoom;

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

void Waveform::renderSmoothWaveform(SDL_Renderer *renderer) const
{
    const auto &session = state->activeDocumentSession;
    const auto &doc = session.document;
    const auto &viewState = state->mainDocumentSessionWindow->getViewState();
    const auto samplesPerPixel = viewState.samplesPerPixel;
    const double halfSampleWidth = 0.5 / samplesPerPixel;
    const int64_t sampleOffset = viewState.sampleOffset;
    const auto sampleData =
        doc.getAudioBuffer()->getImmutableChannelData(channelIndex);
    const auto frameCount = doc.getFrameCount();
    const auto verticalZoom = viewState.verticalZoom;
    const auto widthToUse = getWidth();
    const auto heightToUse = getHeight();
    const auto samplePointSize = getWaveformSamplePointSize(state->pixelScale);
    const auto drawableHeight = heightToUse - samplePointSize;

    const auto input = planWaveformSmoothRenderInput(
        widthToUse, samplesPerPixel, sampleOffset, halfSampleWidth, frameCount,
        [&](const int64_t sampleIndex)
        {
            return sampleData[static_cast<std::size_t>(sampleIndex)];
        });

    if (input.sampleX.empty())
    {
        return;
    }

    SDL_SetRenderDrawColor(renderer, waveformColor.r, waveformColor.g,
                           waveformColor.b, waveformColor.a);

    if (input.sampleX.size() == 1)
    {
        const int x = static_cast<int>(std::lround(input.sampleX.front()));
        const int y = static_cast<int>(
            std::lround(heightToUse / 2.0f -
                        static_cast<float>(input.sampleY.front()) *
                            verticalZoom * drawableHeight / 2.0f));
        SDL_RenderPoint(renderer, x, y);
        return;
    }

    for (std::size_t i = 0; i + 1 < input.sampleX.size(); ++i)
    {
        const int x1 = static_cast<int>(std::lround(input.sampleX[i]));
        const int x2 = static_cast<int>(std::lround(input.sampleX[i + 1]));
        const int y1 = static_cast<int>(
            std::lround(heightToUse / 2.0f -
                        static_cast<float>(input.sampleY[i]) * verticalZoom *
                            drawableHeight / 2.0f));
        const int y2 = static_cast<int>(
            std::lround(heightToUse / 2.0f -
                        static_cast<float>(input.sampleY[i + 1]) *
                            verticalZoom * drawableHeight / 2.0f));

        if (x1 == x2 && y1 == y2)
        {
            SDL_RenderPoint(renderer, x1, y1);
        }
        else
        {
            SDL_RenderLine(renderer, x1, y1, x2, y2);
        }
    }
}

void Waveform::renderBlockWaveform(SDL_Renderer *renderer) const
{
    const auto &viewState = state->mainDocumentSessionWindow->getViewState();
    renderBlockWaveformRange(renderer, 0, getWidth(), getWidth(),
                             viewState.sampleOffset);
}

void Waveform::renderBlockWaveformRange(SDL_Renderer *renderer, int xStart,
                                        int xEndExclusive, int widthToUse,
                                        int64_t sampleOffset) const
{
    auto &session = state->activeDocumentSession;
    auto &doc = session.document;
    const auto &viewState = state->mainDocumentSessionWindow->getViewState();
    SDL_SetRenderDrawColor(renderer, waveformColor.r, waveformColor.g,
                           waveformColor.b, waveformColor.a);

    const double samplesPerPixel = viewState.samplesPerPixel;
    const double blockRenderPhasePx =
        getBlockRenderPhasePixels(sampleOffset, samplesPerPixel);
    const double verticalZoom = viewState.verticalZoom;
    const int heightToUse = getHeight();
    const uint16_t samplePointSize =
        getWaveformSamplePointSize(state->pixelScale);
    const auto drawableHeight = heightToUse - samplePointSize;
    const float scale = (float)(verticalZoom * drawableHeight * 0.5f);
    const int centerY = heightToUse / 2;
    xStart = std::clamp(xStart, 0, widthToUse);
    xEndExclusive = std::clamp(xEndExclusive, 0, widthToUse);
    if (xEndExclusive <= xStart)
    {
        return;
    }

    const auto sampleData =
        doc.getAudioBuffer()->getImmutableChannelData(channelIndex);
    const int64_t frameCount = doc.getFrameCount();

    const bool bypassCache = samplesPerPixel < WaveformCache::BASE_BLOCK_SIZE;

    auto &waveformCache = doc.getWaveformCache(channelIndex);

    if (!bypassCache && waveformCache.levelsCount() == 0)
    {
        waveformCache.rebuildAll(sampleData.data(), frameCount);
    }

    if (!bypassCache)
    {
        const auto &lvl0 =
            waveformCache.getLevel((double)WaveformCache::BASE_BLOCK_SIZE);
        if (lvl0.empty())
        {
            waveformCache.rebuildAll(sampleData.data(), frameCount);
        }
    }

    const int cacheLevel =
        bypassCache ? 0 : waveformCache.getLevelIndex(samplesPerPixel);
    const int64_t samplesPerPeak =
        bypassCache ? 0 : waveformCache.samplesPerPeakForLevel(cacheLevel);
    const std::vector<Peak> *peaks =
        bypassCache ? nullptr : &waveformCache.getLevel(samplesPerPixel);

    auto getPeakForPixel = [&](const int x, Peak &out) -> bool
    {
        double aD = 0.0;
        double bD = 0.0;
        getBlockRenderSampleWindowForPixel(x, sampleOffset, samplesPerPixel, aD,
                                           bD);

        if (bD <= 0.0)
        {
            return false;
        }
        if (aD >= (double)frameCount)
        {
            return false;
        }

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
            out = {minv, maxv};
            return true;
        }

        if (!peaks || peaks->empty())
        {
            return false;
        }

        const int64_t i0 = std::clamp<int64_t>(a / samplesPerPeak, 0,
                                               (int64_t)peaks->size() - 1);
        const int64_t i1 = std::clamp<int64_t>((b - 1) / samplesPerPeak, 0,
                                               (int64_t)peaks->size() - 1);

        float minv = (*peaks)[i0].min;
        float maxv = (*peaks)[i0].max;
        for (int64_t i = i0 + 1; i <= i1; ++i)
        {
            minv = std::min(minv, (*peaks)[i].min);
            maxv = std::max(maxv, (*peaks)[i].max);
        }

        out = {minv, maxv};
        return true;
    };

    int prevX = 0;
    int prevY = 0;
    bool hasPrev = false;
    int lastDrawXi = std::numeric_limits<int>::min();

    const int lookupStart = std::max(0, xStart - 1);
    const int lookupEndExclusive = std::min(widthToUse + 1, xEndExclusive + 1);

    for (int x = lookupStart; x < lookupEndExclusive; ++x)
    {
        Peak p{};
        if (!getPeakForPixel(x, p))
        {
            continue;
        }

        const float drawX = static_cast<float>(x - blockRenderPhasePx);
        const int drawXi = static_cast<int>(std::lround(drawX));
        if (drawXi < 0 || drawXi > widthToUse)
        {
            continue;
        }

        if (drawXi == lastDrawXi)
        {
            continue;
        }

        const int y1 = static_cast<int>(centerY - p.max * scale);
        const int y2 = static_cast<int>(centerY - p.min * scale);
        const int midY = (y1 + y2) / 2;
        const bool connectFromPrevious = hasPrev && prevX != drawXi;

        const bool columnVisible =
            drawXi >= xStart && drawXi < xEndExclusive;
        if (columnVisible)
        {
            if (y1 != y2)
            {
                SDL_RenderLine(renderer, drawXi, y1, drawXi, y2);
            }
            else
            {
                SDL_RenderPoint(renderer, drawXi, y1);
            }
        }

        prevX = drawXi;
        prevY = midY;
        hasPrev = true;
        lastDrawXi = drawXi;
    }
}

void Waveform::drawSelection(SDL_Renderer *renderer) const
{
    const auto &session = state->activeDocumentSession;
    const auto &viewState = state->mainDocumentSessionWindow->getViewState();
    const double samplesPerPixel = viewState.samplesPerPixel;

    const bool isSelected =
        session.selection.isActive() &&
        (viewState.selectedChannels == BOTH ||
         (channelIndex == 0 && viewState.selectedChannels == LEFT) ||
         (channelIndex == 1 && viewState.selectedChannels == RIGHT));

    const int64_t firstSample = session.selection.getStartInt();
    const int64_t lastSample = session.selection.getEndExclusiveInt();
    const int64_t sampleOffset = viewState.sampleOffset;

    if (isSelected && lastSample >= sampleOffset)
    {
        SDL_SetRenderDrawColor(renderer, 0, 64, 255, 128);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

        if (samplesPerPixel >= 1.0)
        {
            const bool bypassCache =
                samplesPerPixel < WaveformCache::BASE_BLOCK_SIZE;
            int64_t samplesPerPeakForDisplay = 1;
            if (!bypassCache)
            {
                const auto &doc = state->activeDocumentSession.document;
                const auto &waveformCache = doc.getWaveformCache(channelIndex);
                const int cacheLevel =
                    waveformCache.getLevelIndex(samplesPerPixel);
                samplesPerPeakForDisplay =
                    waveformCache.samplesPerPeakForLevel(cacheLevel);
            }

            SDL_FRect selectionRect{};
            if (computeBlockModeSelectionRect(firstSample, lastSample,
                                              sampleOffset, samplesPerPixel,
                                              getWidth(), getHeight(),
                                              selectionRect,
                                              samplesPerPeakForDisplay, true))
            {
                SDL_RenderFillRect(renderer, &selectionRect);
            }
            return;
        }

        const auto selectionRect = planWaveformLinearSelectionRect(
            isSelected, firstSample, lastSample, sampleOffset, samplesPerPixel,
            getHeight());
        if (selectionRect.visible)
        {
            SDL_RenderFillRect(renderer, &selectionRect.rect);
        }
    }
}

void Waveform::drawHighlight(SDL_Renderer *renderer) const
{
    const auto window = getWindow();
    if (const auto waveformsUnderlay = dynamic_cast<WaveformsUnderlay *>(
            window ? window->getCapturingComponent() : nullptr);
        waveformsUnderlay != nullptr)
    {
        return;
    }

    const auto &viewState = state->mainDocumentSessionWindow->getViewState();
    const auto samplesPerPixel = viewState.samplesPerPixel;

    if (shouldShowSamplePoints(samplesPerPixel, state->pixelScale) &&
        samplePosUnderCursor.has_value())
    {
        const auto &session = state->activeDocumentSession;
        const auto &document = session.document;
        const auto sampleOffset = viewState.sampleOffset;

        const auto samplePoint = dynamic_cast<SamplePoint *>(
            window ? window->getCapturingComponent() : nullptr);
        const int64_t sampleIndex = samplePoint == nullptr
                                        ? *samplePosUnderCursor
                                        : samplePoint->getSampleIndex();

        const auto highlightRect = planWaveformHighlightRect(
            true, sampleIndex, document.getFrameCount(), sampleOffset,
            samplesPerPixel, getHeight());
        if (highlightRect.visible)
        {
            SDL_SetRenderDrawColor(renderer, 0, 128, 255, 100);
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_RenderFillRect(renderer, &highlightRect.rect);
        }
    }
}

void Waveform::onDraw(SDL_Renderer *renderer)
{
    if (ensureBaseTexture(renderer))
    {
        SDL_RenderTexture(renderer, cachedBaseTexture,
                          &cachedBaseTextureSourceRect, nullptr);
    }
    else
    {
        drawBaseWaveformContents(renderer);
    }

    drawSelection(renderer);

    drawHighlight(renderer);

    drawCursor(renderer);

    drawPlaybackPosition(renderer);
}

void Waveform::drawPlaybackPosition(SDL_Renderer *renderer) const
{
    const auto &viewState = state->mainDocumentSessionWindow->getViewState();
    const auto marker = planWaveformPlaybackMarker(
        playbackPosition, viewState.sampleOffset, viewState.samplesPerPixel,
        getWidth());
    if (marker.visible)
    {
        SDL_SetRenderDrawColor(renderer, 0, 200, 200, 255);
        SDL_RenderLine(renderer, marker.x, 0, marker.x, getHeight());
    }
}

void Waveform::drawCursor(SDL_Renderer *renderer) const
{
    const auto &session = state->activeDocumentSession;
    const auto &viewState = state->mainDocumentSessionWindow->getViewState();
    const auto marker = planWaveformCursorMarker(
        session.selection.isActive(), session.cursor, viewState.sampleOffset,
        viewState.samplesPerPixel, getWidth());
    if (marker.visible)
    {
        SDL_SetRenderDrawColor(renderer, 188, 188, 0, 255);

        const int yInterval = 10 * (1.f / state->pixelScale);

        for (int i = yInterval; i < getHeight(); i += yInterval)
        {
            SDL_RenderPoint(renderer, marker.x, i);
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
}

void Waveform::setPlaybackPosition(const int64_t newPlaybackPosition)
{
    const bool wasPlaybackActive = playbackPosition >= 0;
    const bool isPlaybackActive = newPlaybackPosition >= 0;
    if (playbackPosition == newPlaybackPosition)
    {
        return;
    }
    playbackPosition = newPlaybackPosition;
    if (wasPlaybackActive != isPlaybackActive)
    {
        updateSamplePoints();
    }
    setDirty();
}

void Waveform::mouseLeave()
{
    auto &viewState = state->mainDocumentSessionWindow->getViewState();
    viewState.hoveringOverChannels = BOTH;

    for (auto &c : getChildren())
    {
        // Mouse just entered a sample point.
        // We don't want to clear the highlight in this case.
        if (window && window->getComponentUnderMouse() == c.get())
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
    if (samplePosUnderCursor.has_value() &&
        *samplePosUnderCursor == samplePosUnderCursorToUse)
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
