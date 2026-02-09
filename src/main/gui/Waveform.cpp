#include "Waveform.hpp"

#include "audio/AudioDevices.hpp"
#include "WaveformsUnderlay.hpp"
#include "WaveformCache.hpp"
#include "Window.hpp"

#include "smooth_line.hpp"

#include <limits>
#include <cmath>
#include <algorithm>

using namespace cupuacu::gui;

namespace
{
    constexpr bool kDrawBlockConnectorLines = true;
}

Waveform::Waveform(State *state, const uint8_t channelIndexToUse)
    : Component(state, "Waveform"), channelIndex(channelIndexToUse)
{
}

Waveform::~Waveform()
{
    destroyBlockWaveformTextures();
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
    destroyBlockWaveformTextures();
    updateSamplePoints();
}

void Waveform::updateSamplePoints()
{
    removeAllChildren();
    const auto &viewState = state->mainDocumentSessionWindow->getViewState();
    const bool playbackActive = playbackPosition >= 0;

    if (!playbackActive &&
        shouldShowSamplePoints(viewState.samplesPerPixel, state->pixelScale))
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
    return samplesPerPixel < (float)pixelScale / 40.f;
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
    const double samplesPerPixel = viewState.samplesPerPixel;
    const int64_t sampleOffset = viewState.sampleOffset;
    const uint8_t pixelScale = state->pixelScale;
    const double verticalZoom = viewState.verticalZoom;
    const float halfSampleWidth = 0.5f / samplesPerPixel;

    const int64_t neededInputSamples =
        static_cast<int64_t>(std::round(getWidth() * samplesPerPixel));
    const int64_t availableSamples = doc.getFrameCount() - sampleOffset;
    const int64_t actualInputSamples =
        std::min(neededInputSamples, availableSamples);

    if (actualInputSamples < 1)
    {
        return {};
    }

    std::vector<std::unique_ptr<SamplePoint>> result;
    const uint16_t samplePointSize = getSamplePointSize(pixelScale);

    const auto sampleData =
        doc.getAudioBuffer()->getImmutableChannelData(channelIndex);

    for (int i = 0; i < actualInputSamples; ++i)
    {
        const int xPos =
            static_cast<int>(i / samplesPerPixel + halfSampleWidth);
        const int yPos =
            getYPosForSampleValue(sampleData[sampleOffset + i], getHeight(),
                                  verticalZoom, samplePointSize);

        auto samplePoint = std::make_unique<SamplePoint>(state, channelIndex,
                                                         sampleOffset + i);
        samplePoint->setBounds(std::max(0, xPos - samplePointSize / 2),
                               yPos - samplePointSize / 2, samplePointSize,
                               samplePointSize);
        result.push_back(std::move(samplePoint));
    }

    return result;
}

void Waveform::drawHorizontalLines(SDL_Renderer *renderer) const
{
    const auto samplePointSize = getSamplePointSize(state->pixelScale);
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
    const auto samplePointSize = getSamplePointSize(state->pixelScale);
    const auto drawableHeight = heightToUse - samplePointSize;

    const int64_t neededInputSamples =
        static_cast<int64_t>(std::ceil((widthToUse + 1) * samplesPerPixel));
    const int64_t availableSamples = frameCount - sampleOffset;
    const int64_t actualInputSamples =
        std::min(neededInputSamples, availableSamples);

    if (actualInputSamples < 1)
    {
        return;
    }

    smoothXBuffer.resize(static_cast<std::size_t>(actualInputSamples));
    smoothYBuffer.resize(static_cast<std::size_t>(actualInputSamples));

    for (int i = 0; i < actualInputSamples; ++i)
    {
        smoothXBuffer[static_cast<std::size_t>(i)] =
            i / samplesPerPixel + halfSampleWidth;
        smoothYBuffer[static_cast<std::size_t>(i)] =
            static_cast<double>(sampleData[sampleOffset + i]);
    }

    const int numPoints = widthToUse + 1;
    smoothQueryBuffer.resize(static_cast<std::size_t>(numPoints));

    for (int i = 0; i < numPoints; ++i)
    {
        smoothQueryBuffer[static_cast<std::size_t>(i)] =
            static_cast<double>(i);
    }

    const auto smoothened = splineInterpolateNonUniform(
        smoothXBuffer, smoothYBuffer, smoothQueryBuffer);

    for (int i = 0; i < numPoints - 1; ++i)
    {
        const float x1 = static_cast<float>(smoothQueryBuffer[static_cast<std::size_t>(i)]);
        const float x2 = static_cast<float>(
            smoothQueryBuffer[static_cast<std::size_t>(i + 1)]);

        const float y1f = heightToUse / 2.0f -
                          smoothened[i] * verticalZoom * drawableHeight / 2.0f;
        const float y2f = heightToUse / 2.0f - smoothened[i + 1] *
                                                   verticalZoom *
                                                   drawableHeight / 2.0f;

        constexpr float thickness = 1.0f;

        float dx = x2 - x1;
        float dy = y2f - y1f;
        const float len = std::sqrt(dx * dx + dy * dy);
        if (len == 0.0f)
        {
            continue;
        }

        dx /= len;
        dy /= len;

        const float px = -dy * thickness * 0.5f;
        const float py = dx * thickness * 0.5f;

        SDL_Vertex verts[4];
        verts[0].position = {x1 - px, y1f - py};
        verts[1].position = {x1 + px, y1f + py};
        verts[2].position = {x2 + px, y2f + py};
        verts[3].position = {x2 - px, y2f - py};

        for (int j = 0; j < 4; ++j)
        {
            verts[j].color = waveformFColor;
            verts[j].tex_coord = {0, 0};
        }

        const int indices[6] = {0, 1, 2, 0, 2, 3};
        SDL_RenderGeometry(renderer, nullptr, verts, 4, indices, 6);
    }
}

void Waveform::renderBlockWaveformRange(SDL_Renderer *renderer, const int xStart,
                                        const int xEndExclusive,
                                        const int64_t sampleOffsetToUse) const
{
    auto &doc = state->activeDocumentSession.document;
    const auto &viewState = state->mainDocumentSessionWindow->getViewState();
    SDL_SetRenderDrawColor(renderer, waveformColor.r, waveformColor.g,
                           waveformColor.b, waveformColor.a);

    const double samplesPerPixel = viewState.samplesPerPixel;
    const double blockRenderPhasePx =
        getBlockRenderPhasePixels(sampleOffsetToUse, samplesPerPixel);
    const double verticalZoom = viewState.verticalZoom;
    const int widthToUse = getWidth();
    const int heightToUse = getHeight();
    const uint16_t samplePointSize = getSamplePointSize(state->pixelScale);
    const auto drawableHeight = heightToUse - samplePointSize;
    const float scale = (float)(verticalZoom * drawableHeight * 0.5f);
    const int centerY = heightToUse / 2;

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
    const auto &peaks = bypassCache ? *(const std::vector<Peak> *)nullptr
                                    : waveformCache.getLevel(samplesPerPixel);

    auto getPeakForPixel = [&](const int x, Peak &out) -> bool
    {
        double aD = 0.0;
        double bD = 0.0;
        getBlockRenderSampleWindowForPixel(x, sampleOffsetToUse, samplesPerPixel,
                                           aD, bD);

        if (bD <= 0.0 || aD >= (double)frameCount)
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
                const float v = sampleData[i];
                minv = std::min(minv, v);
                maxv = std::max(maxv, v);
            }
            out = {minv, maxv};
            return true;
        }

        if (peaks.empty())
        {
            return false;
        }

        const int64_t i0 = std::clamp<int64_t>(a / samplesPerPeak, 0,
                                               (int64_t)peaks.size() - 1);
        const int64_t i1 = std::clamp<int64_t>((b - 1) / samplesPerPeak, 0,
                                               (int64_t)peaks.size() - 1);

        float minv = peaks[i0].min;
        float maxv = peaks[i0].max;
        for (int64_t i = i0 + 1; i <= i1; ++i)
        {
            minv = std::min(minv, peaks[i].min);
            maxv = std::max(maxv, peaks[i].max);
        }

        out = {minv, maxv};
        return true;
    };

    int lastDrawXi = std::numeric_limits<int>::min();
    int prevX = 0;
    int prevY = 0;
    bool hasPrev = false;
    const int loopStart = std::max(0, xStart);
    const int loopEnd = std::min(widthToUse + 1, std::max(loopStart, xEndExclusive));

    for (int x = loopStart; x < loopEnd; ++x)
    {
        Peak p{};
        if (!getPeakForPixel(x, p))
        {
            continue;
        }

        const float drawX = static_cast<float>(x - blockRenderPhasePx);
        const int drawXi = static_cast<int>(std::lround(drawX));
        if (drawXi < 0 || drawXi > widthToUse || drawXi == lastDrawXi)
        {
            continue;
        }

        const int y1 = (int)(centerY - p.max * scale);
        const int y2 = (int)(centerY - p.min * scale);
        const int midY = (y1 + y2) / 2;
        if (y1 != y2)
        {
            SDL_RenderLine(renderer, drawXi, y1, drawXi, y2);
        }
        else
        {
            SDL_RenderPoint(renderer, drawXi, y1);
        }

        if (kDrawBlockConnectorLines && hasPrev && prevX != drawXi)
        {
            SDL_RenderLine(renderer, prevX, prevY, drawXi, midY);
        }

        prevX = drawXi;
        prevY = midY;
        hasPrev = true;
        lastDrawXi = drawXi;
    }
}

void Waveform::renderBlockWaveform(SDL_Renderer *renderer) const
{
    const auto &viewState = state->mainDocumentSessionWindow->getViewState();
    renderBlockWaveformRange(renderer, 0, getWidth() + 1, viewState.sampleOffset);
}

void Waveform::destroyBlockWaveformTextures() const
{
    if (blockWaveformTexture)
    {
        SDL_DestroyTexture(blockWaveformTexture);
        blockWaveformTexture = nullptr;
    }
    if (blockWaveformScratchTexture)
    {
        SDL_DestroyTexture(blockWaveformScratchTexture);
        blockWaveformScratchTexture = nullptr;
    }
    blockWaveformTextureValid = false;
    blockWaveformTextureWidth = 0;
    blockWaveformTextureHeight = 0;
}

void Waveform::renderCachedBlockWaveform(SDL_Renderer *renderer) const
{
    SDL_Texture *const previousTarget = SDL_GetRenderTarget(renderer);
    SDL_Rect previousViewport{};
    SDL_GetRenderViewport(renderer, &previousViewport);

    const auto &viewState = state->mainDocumentSessionWindow->getViewState();
    const int widthToUse = getWidth();
    const int heightToUse = getHeight();
    const int64_t frameCount = state->activeDocumentSession.document.getFrameCount();

    if (widthToUse <= 0 || heightToUse <= 0)
    {
        return;
    }

    if (!blockWaveformTexture || !blockWaveformScratchTexture ||
        blockWaveformTextureWidth != widthToUse ||
        blockWaveformTextureHeight != heightToUse)
    {
        destroyBlockWaveformTextures();
        blockWaveformTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888,
                                                 SDL_TEXTUREACCESS_TARGET,
                                                 widthToUse, heightToUse);
        blockWaveformScratchTexture =
            SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888,
                              SDL_TEXTUREACCESS_TARGET, widthToUse,
                              heightToUse);
        if (!blockWaveformTexture || !blockWaveformScratchTexture)
        {
            destroyBlockWaveformTextures();
            renderBlockWaveform(renderer);
            return;
        }
        SDL_SetTextureScaleMode(blockWaveformTexture, SDL_SCALEMODE_NEAREST);
        SDL_SetTextureScaleMode(blockWaveformScratchTexture, SDL_SCALEMODE_NEAREST);
        blockWaveformTextureWidth = widthToUse;
        blockWaveformTextureHeight = heightToUse;
        blockWaveformTextureValid = false;
    }

    const bool paramsChanged =
        !blockWaveformTextureValid ||
        blockWaveformTextureSamplesPerPixel != viewState.samplesPerPixel ||
        blockWaveformTextureVerticalZoom != viewState.verticalZoom ||
        blockWaveformTextureFrameCount != frameCount;

    if (paramsChanged)
    {
        SDL_SetRenderTarget(renderer, blockWaveformTexture);
        SDL_SetRenderViewport(renderer, nullptr);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        renderBlockWaveformRange(renderer, 0, widthToUse + 1, viewState.sampleOffset);

        blockWaveformTextureSampleOffset = viewState.sampleOffset;
        blockWaveformTextureSamplesPerPixel = viewState.samplesPerPixel;
        blockWaveformTextureVerticalZoom = viewState.verticalZoom;
        blockWaveformTextureFrameCount = frameCount;
        blockWaveformTextureValid = true;
    }
    else
    {
        const int64_t deltaSamples =
            viewState.sampleOffset - blockWaveformTextureSampleOffset;
        const int pixelDelta = static_cast<int>(
            std::lround((double)deltaSamples / viewState.samplesPerPixel));

        if (pixelDelta != 0)
        {
            const int shift = -pixelDelta;
            if (std::abs(shift) >= widthToUse)
            {
                SDL_SetRenderTarget(renderer, blockWaveformTexture);
                SDL_SetRenderViewport(renderer, nullptr);
                SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
                SDL_RenderClear(renderer);
                renderBlockWaveformRange(renderer, 0, widthToUse + 1,
                                        viewState.sampleOffset);
            }
            else
            {
                SDL_SetRenderTarget(renderer, blockWaveformScratchTexture);
                SDL_SetRenderViewport(renderer, nullptr);
                SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
                SDL_RenderClear(renderer);

                SDL_FRect srcRect{};
                SDL_FRect dstRect{};
                if (shift < 0)
                {
                    const float k = static_cast<float>(-shift);
                    srcRect = {k, 0.0f, static_cast<float>(widthToUse) - k,
                               static_cast<float>(heightToUse)};
                    dstRect = {0.0f, 0.0f, srcRect.w, srcRect.h};
                }
                else
                {
                    const float k = static_cast<float>(shift);
                    srcRect = {0.0f, 0.0f, static_cast<float>(widthToUse) - k,
                               static_cast<float>(heightToUse)};
                    dstRect = {k, 0.0f, srcRect.w, srcRect.h};
                }

                SDL_RenderTexture(renderer, blockWaveformTexture, &srcRect, &dstRect);

                if (shift < 0)
                {
                    renderBlockWaveformRange(renderer, widthToUse + shift,
                                            widthToUse + 1,
                                            viewState.sampleOffset);
                }
                else
                {
                    renderBlockWaveformRange(renderer, 0, shift + 1,
                                            viewState.sampleOffset);
                }

                std::swap(blockWaveformTexture, blockWaveformScratchTexture);
            }
            blockWaveformTextureSampleOffset = viewState.sampleOffset;
        }
    }

    SDL_SetRenderTarget(renderer, previousTarget);
    SDL_SetRenderViewport(renderer, &previousViewport);
    SDL_RenderTexture(renderer, blockWaveformTexture, nullptr, nullptr);
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
    const int64_t lastSample = session.selection.getEndInt() + 1;
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

        const float startX =
            firstSample <= sampleOffset
                ? 0
                : getXPosForSampleIndex(firstSample, sampleOffset,
                                        samplesPerPixel);
        const float endX =
            getXPosForSampleIndex(lastSample, sampleOffset, samplesPerPixel);
        const float selectionWidth =
            std::abs(endX - startX) < 1.0f ? 1.0f : (endX - startX);

        const SDL_FRect selectionRect = {startX, 0.0f, selectionWidth,
                                         (float)getHeight()};
        SDL_RenderFillRect(renderer, &selectionRect);
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

        if (sampleIndex < document.getFrameCount())
        {
            const float xPos = getXPosForSampleIndex(sampleIndex, sampleOffset,
                                                     samplesPerPixel);
            const float sampleWidth = 1.0f / samplesPerPixel;

            SDL_SetRenderDrawColor(renderer, 0, 128, 255, 100);
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            const SDL_FRect sampleRect = {xPos, 0.0f, sampleWidth,
                                          (float)getHeight()};
            SDL_RenderFillRect(renderer, &sampleRect);
        }
    }
}

void Waveform::onDraw(SDL_Renderer *renderer)
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
        renderCachedBlockWaveform(renderer);
    }

    drawSelection(renderer);

    drawHighlight(renderer);

    drawCursor(renderer);

    drawPlaybackPosition(renderer);
}

void Waveform::drawPlaybackPosition(SDL_Renderer *renderer) const
{
    if (playbackPosition == -1)
    {
        return;
    }

    const auto &viewState = state->mainDocumentSessionWindow->getViewState();
    const int32_t playbackXPos = getXPosForSampleIndex(
        playbackPosition, viewState.sampleOffset, viewState.samplesPerPixel);

    if (playbackXPos >= 0 && playbackXPos <= getWidth())
    {
        SDL_SetRenderDrawColor(renderer, 0, 200, 200, 255);
        SDL_RenderLine(renderer, playbackXPos, 0, playbackXPos, getHeight());
    }
}

void Waveform::drawCursor(SDL_Renderer *renderer) const
{
    const auto &session = state->activeDocumentSession;
    const auto &viewState = state->mainDocumentSessionWindow->getViewState();
    if (session.selection.isActive())
    {
        return;
    }

    const int32_t cursorXPos = getXPosForSampleIndex(
        session.cursor, viewState.sampleOffset, viewState.samplesPerPixel);

    if (cursorXPos >= 0 && cursorXPos <= getWidth())
    {
        SDL_SetRenderDrawColor(renderer, 188, 188, 0, 255);

        const int yInterval = 10 * (1.f / state->pixelScale);

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
