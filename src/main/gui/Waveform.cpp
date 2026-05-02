#include "Waveform.hpp"

#include "audio/AudioDevices.hpp"
#include "WaveformBlockRenderPlanning.hpp"
#include "WaveformOverviewPlanning.hpp"
#include "WaveformSamplePointPlanning.hpp"
#include "WaveformSmoothRenderPlanning.hpp"
#include "WaveformsUnderlay.hpp"
#include "WaveformCache.hpp"
#include "WaveformVisualState.hpp"
#include "Window.hpp"

#include "smooth_line.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

using namespace cupuacu::gui;

namespace
{
    constexpr int kBackgroundBlockRenderChunkWidth = 64;

    void appendColoredQuad(std::vector<SDL_Vertex> &vertices,
                           std::vector<int> &indices,
                           const std::array<SDL_FPoint, 4> &positions,
                           const SDL_FColor color)
    {
        const int baseIndex = static_cast<int>(vertices.size());
        for (const auto &position : positions)
        {
            vertices.push_back({position, color, {0.0f, 0.0f}});
        }

        const int quadIndices[6] = {0, 1, 2, 0, 2, 3};
        for (const int index : quadIndices)
        {
            indices.push_back(baseIndex + index);
        }
    }

    void appendPointQuad(std::vector<SDL_Vertex> &vertices,
                         std::vector<int> &indices, const int x, const int y,
                         const SDL_FColor color)
    {
        appendColoredQuad(
            vertices, indices,
            {SDL_FPoint{static_cast<float>(x) - 0.5f,
                        static_cast<float>(y) - 0.5f},
             SDL_FPoint{static_cast<float>(x) + 0.5f,
                        static_cast<float>(y) - 0.5f},
             SDL_FPoint{static_cast<float>(x) + 0.5f,
                        static_cast<float>(y) + 0.5f},
             SDL_FPoint{static_cast<float>(x) - 0.5f,
                        static_cast<float>(y) + 0.5f}},
            color);
    }

    void appendLineQuad(std::vector<SDL_Vertex> &vertices,
                        std::vector<int> &indices, const float x1,
                        const float y1, const float x2, const float y2,
                        const SDL_FColor color)
    {
        const auto quad =
            planWaveformSmoothSegmentQuad(x1, x2, y1, y2, 1.0f);
        if (!quad)
        {
            appendPointQuad(vertices, indices, static_cast<int>(std::lround(x1)),
                            static_cast<int>(std::lround(y1)), color);
            return;
        }

        appendColoredQuad(vertices, indices, quad->vertices, color);
    }

}

Waveform::Waveform(State *state, const uint8_t channelIndexToUse)
    : Component(state, "Waveform"), channelIndex(channelIndexToUse)
{
    ensureBackgroundBlockRenderWorker();
}

Waveform::~Waveform()
{
    backgroundBlockRenderWorker.reset();
    invalidateBaseTexture();
}

uint8_t Waveform::getChannelIndex() const
{
    return channelIndex;
}

bool Waveform::hasRenderableChannel() const
{
    if (!state)
    {
        return false;
    }

    const auto &document = state->getActiveDocumentSession().document;
    return channelIndex < document.getChannelCount() &&
           document.getFrameCount() > 0 &&
           document.getAudioBuffer() != nullptr;
}

void Waveform::resized()
{
    updateSamplePoints();

    const auto newKey = computeBaseTextureCacheKey();
    if (newKey.samplesPerPixel >= 1.0)
    {
        requestBackgroundBlockRenderPlan(
            chooseBaseTextureTargetKey(newKey, true));
    }
}

void Waveform::invalidateBaseTexture() const
{
    cachedBaseTextureValid = false;
    requestedBackgroundBlockRenderKey.reset();
    backgroundBlockRenderProgress.reset();
    latestBackgroundBlockRenderGeneration = 0;
    destroyTexture(cachedBaseTexture);
    for (auto &stored : storedBlockTextures)
    {
        destroyTexture(stored.texture);
    }
    storedBlockTextures.clear();
}

Waveform::BaseTextureCacheKey Waveform::computeBaseTextureCacheKey() const
{
    const auto &viewState = state->getActiveViewState();
    const auto &doc = state->getActiveDocumentSession().document;
    const BaseTextureCacheKey key{
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
    return key;
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

Waveform::BaseTextureCacheKey Waveform::chooseBaseTextureTargetKey(
    const BaseTextureCacheKey &newKey, const bool allowBlockCoverageReuse) const
{
    if (!allowBlockCoverageReuse)
    {
        return newKey;
    }

    auto targetKey = makeCurrentBlockTextureCoverageKey(newKey);
    const double sourceX =
        (static_cast<double>(newKey.sampleOffset) -
         static_cast<double>(targetKey.sampleOffset)) /
        newKey.samplesPerPixel;
    const bool hasReusableCrop =
        sourceX >= 0.0 &&
        sourceX + static_cast<double>(newKey.viewWidth) <=
            static_cast<double>(targetKey.width);
    return hasReusableCrop ? targetKey : newKey;
}

bool Waveform::canRenderCurrentViewFromCachedBlockTexture(
    const BaseTextureCacheKey &currentViewKey,
    const BaseTextureCacheKey &sourceTextureKey, SDL_FRect &outSourceRect) const
{
    if (currentViewKey.samplesPerPixel < 1.0)
    {
        return false;
    }

    if (sourceTextureKey.viewWidth != currentViewKey.viewWidth ||
        sourceTextureKey.viewHeight != currentViewKey.viewHeight ||
        sourceTextureKey.height != currentViewKey.viewHeight ||
        sourceTextureKey.frameCount != currentViewKey.frameCount ||
        sourceTextureKey.waveformDataVersion != currentViewKey.waveformDataVersion ||
        sourceTextureKey.pixelScale != currentViewKey.pixelScale ||
        sourceTextureKey.samplesPerPixel != currentViewKey.samplesPerPixel ||
        sourceTextureKey.verticalZoom != currentViewKey.verticalZoom)
    {
        return false;
    }

    const double sourceX =
        (static_cast<double>(currentViewKey.sampleOffset) -
         static_cast<double>(sourceTextureKey.sampleOffset)) /
        currentViewKey.samplesPerPixel;
    if (sourceX < 0.0)
    {
        return false;
    }

    if (sourceX + static_cast<double>(currentViewKey.viewWidth) >
        static_cast<double>(sourceTextureKey.width))
    {
        return false;
    }

    outSourceRect = {static_cast<float>(sourceX), 0.0f,
                     static_cast<float>(currentViewKey.viewWidth),
                     static_cast<float>(currentViewKey.viewHeight)};
    return true;
}

bool Waveform::ensureBaseTextureStorage(
    SDL_Renderer *renderer, const BaseTextureCacheKey &targetKey) const
{
    if (cachedBaseTexture && cachedBaseTextureKey.width == targetKey.width &&
        cachedBaseTextureKey.height == targetKey.height)
    {
        return true;
    }

    storeCurrentBlockTexture();
    destroyTexture(cachedBaseTexture);

    cachedBaseTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888,
                                          SDL_TEXTUREACCESS_TARGET,
                                          targetKey.width, targetKey.height);
    if (!cachedBaseTexture)
    {
        cachedBaseTextureValid = false;
        return false;
    }

    SDL_SetTextureScaleMode(cachedBaseTexture, SDL_SCALEMODE_NEAREST);
    return true;
}

void Waveform::renderBaseTexture(SDL_Renderer *renderer,
                                 const BaseTextureCacheKey &targetKey,
                                 const bool isBlockMode) const
{
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
}

void Waveform::renderBaseTextureFromBackgroundPlan(
    SDL_Renderer *renderer, const BackgroundBlockRenderProgress &plan) const
{
    if (!cachedBaseTexture)
    {
        return;
    }

    SDL_Texture *previousTarget = SDL_GetRenderTarget(renderer);
    SDL_Rect previousViewport{};
    SDL_GetRenderViewport(renderer, &previousViewport);

    SDL_SetRenderTarget(renderer, cachedBaseTexture);
    const SDL_Rect localViewport{0, 0, plan.key.width, plan.key.height};
    SDL_SetRenderViewport(renderer, &localViewport);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderFillRect(renderer, nullptr);
    drawHorizontalLines(renderer);
    if (!plan.vertices.empty() && !plan.indices.empty())
    {
        SDL_RenderGeometry(renderer, nullptr, plan.vertices.data(),
                           static_cast<int>(plan.vertices.size()),
                           plan.indices.data(),
                           static_cast<int>(plan.indices.size()));
    }

    SDL_SetRenderTarget(renderer, previousTarget);
    SDL_SetRenderViewport(renderer, &previousViewport);
}

std::optional<Waveform::BackgroundBlockRenderRequest>
Waveform::captureBackgroundBlockRenderRequest(
    const BaseTextureCacheKey &targetKey) const
{
    if (!state || targetKey.width <= 0 || targetKey.height <= 0)
    {
        return std::nullopt;
    }

    const auto &document = state->getActiveDocumentSession().document;
    auto lease = document.acquireReadLease();
    const int64_t frameCount = lease.getFrameCount();
    const int64_t channelCount = lease.getChannelCount();
    if (channelIndex >= channelCount || frameCount <= 0)
    {
        return std::nullopt;
    }

    const auto &waveformCache = document.getWaveformCache(channelIndex);
    const auto inputPlan = planBackgroundBlockRenderInput(
        frameCount, targetKey.sampleOffset, targetKey.samplesPerPixel,
        targetKey.width, targetKey.pixelScale, waveformCache);

    std::vector<float> rawSamples;
    std::vector<Peak> cachedPeaks;
    if (inputPlan.bypassCache)
    {
        rawSamples.reserve(static_cast<std::size_t>(
            std::max<int64_t>(0, inputPlan.rawSampleEndExclusive -
                                     inputPlan.rawSampleStart)));
        for (int64_t sample = inputPlan.rawSampleStart;
             sample < inputPlan.rawSampleEndExclusive; ++sample)
        {
            rawSamples.push_back(lease.getSample(channelIndex, sample));
        }
    }
    else
    {
        const auto &selectedLevel =
            waveformCache.getLevelByIndex(inputPlan.cacheLevel);
        cachedPeaks = selectedLevel;
    }
    return BackgroundBlockRenderRequest{
        .key = targetKey,
        .frameCount = frameCount,
        .uiScale = state->uiScale,
        .bypassCache = inputPlan.bypassCache,
        .cacheLevel = inputPlan.cacheLevel,
        .samplesPerPeak = inputPlan.samplesPerPeak,
        .rawSampleStart = inputPlan.rawSampleStart,
        .rawSamples = std::move(rawSamples),
        .cachedPeaks = std::move(cachedPeaks),
    };
}

void Waveform::processBackgroundBlockRenderRequest(
    const BackgroundBlockRenderRequest &request, const std::uint64_t generation,
    const BackgroundBlockRenderWorker::CancelCheck &isCanceled,
    const BackgroundBlockRenderWorker::PublishFn &publish) const
{
    (void)generation;
    const double blockRenderPhasePx =
        getBlockRenderPhasePixels(request.key.sampleOffset,
                                  request.key.samplesPerPixel);
    const uint16_t samplePointSize =
        getWaveformSamplePointSize(request.key.pixelScale, request.uiScale);
    const int drawableHeight = request.key.height - samplePointSize;
    const float scale = static_cast<float>(
        request.key.verticalZoom * static_cast<double>(drawableHeight) * 0.5);
    const int centerY = request.key.height / 2;

    auto computePeakForWindow = [&](const double startSampleInclusive,
                                    const double endSampleExclusive,
                                    Peak &outPeak) -> bool
    {
        const int64_t frameCount = request.frameCount;
        if (frameCount <= 0 || endSampleExclusive <= 0.0 ||
            startSampleInclusive >= static_cast<double>(frameCount))
        {
            return false;
        }

        const auto sampleEndExclusive =
            request.rawSampleStart +
            static_cast<int64_t>(request.rawSamples.size());
        const auto sampleAt = [&](const int64_t sampleIndex) -> float
        {
            const auto localIndex = sampleIndex - request.rawSampleStart;
            if (localIndex < 0 ||
                localIndex >= static_cast<int64_t>(request.rawSamples.size()))
            {
                return 0.0f;
            }
            return request.rawSamples[static_cast<std::size_t>(localIndex)];
        };

        int64_t a = static_cast<int64_t>(std::floor(startSampleInclusive));
        int64_t b = static_cast<int64_t>(std::floor(endSampleExclusive));
        a = std::clamp<int64_t>(a, 0, frameCount - 1);
        b = std::clamp<int64_t>(b, a + 1, frameCount);

        if (request.bypassCache)
        {
            if (a < request.rawSampleStart || b > sampleEndExclusive)
            {
                return false;
            }
            float minv = sampleAt(a);
            float maxv = minv;
            for (int64_t i = a + 1; i < b; ++i)
            {
                const float v = sampleAt(i);
                minv = std::min(minv, v);
                maxv = std::max(maxv, v);
            }
            outPeak = {minv, maxv};
            return true;
        }

        if (request.samplesPerPeak <= 0 || request.cachedPeaks.empty())
        {
            return false;
        }

        const int64_t firstPeakIndex = std::clamp<int64_t>(
            a / request.samplesPerPeak, 0,
            static_cast<int64_t>(request.cachedPeaks.size()) - 1);
        const int64_t lastPeakIndexExclusive = std::clamp<int64_t>(
            static_cast<int64_t>(
                std::ceil(static_cast<double>(b) /
                          static_cast<double>(request.samplesPerPeak))),
            firstPeakIndex + 1,
            static_cast<int64_t>(request.cachedPeaks.size()));

        Peak peak =
            request.cachedPeaks[static_cast<std::size_t>(firstPeakIndex)];
        for (int64_t i = firstPeakIndex + 1; i < lastPeakIndexExclusive; ++i)
        {
            peak.min = std::min(
                peak.min,
                request.cachedPeaks[static_cast<std::size_t>(i)].min);
            peak.max = std::max(
                peak.max,
                request.cachedPeaks[static_cast<std::size_t>(i)].max);
        }

        outPeak = peak;
        return true;
    };

    BackgroundBlockRenderChunk chunk{
        .key = request.key,
        .reset = true,
    };
    chunk.vertices.reserve(
        static_cast<std::size_t>(kBackgroundBlockRenderChunkWidth) * 8);
    chunk.indices.reserve(
        static_cast<std::size_t>(kBackgroundBlockRenderChunkWidth) * 12);
    const auto publishChunk = [&]()
    {
        if (!chunk.reset && chunk.vertices.empty() && chunk.indices.empty())
        {
            return;
        }
        publish(std::move(chunk));
        chunk = BackgroundBlockRenderChunk{
            .key = request.key,
            .reset = false,
        };
        chunk.vertices.reserve(
            static_cast<std::size_t>(kBackgroundBlockRenderChunkWidth) * 8);
        chunk.indices.reserve(
            static_cast<std::size_t>(kBackgroundBlockRenderChunkWidth) * 12);
    };

    int prevX = 0;
    int prevY = 0;
    bool hasPrev = false;
    int lastDrawXi = std::numeric_limits<int>::min();

    for (int x = 0; x < request.key.width + 1; ++x)
    {
        if (x > 0 && (x % kBackgroundBlockRenderChunkWidth) == 0)
        {
            publishChunk();
            if (isCanceled())
            {
                return;
            }
        }

        const float drawX = static_cast<float>(x - blockRenderPhasePx);
        const int drawXi = static_cast<int>(std::lround(drawX));
        if (drawXi < 0 || drawXi > request.key.width)
        {
            continue;
        }
        if (drawXi == lastDrawXi)
        {
            continue;
        }

        double aD = 0.0;
        double bD = 0.0;
        getBlockRenderSampleWindowForPixel(
            x, request.key.sampleOffset, request.key.samplesPerPixel, aD, bD);
        if (bD <= 0.0 || aD >= static_cast<double>(request.frameCount))
        {
            continue;
        }

        Peak p{};
        if (!computePeakForWindow(aD, bD, p))
        {
            continue;
        }

        const int y1 = static_cast<int>(centerY - p.max * scale);
        const int y2 = static_cast<int>(centerY - p.min * scale);
        const int midY = (y1 + y2) / 2;
        const bool connectFromPrevious = hasPrev && prevX != drawXi;

        if (y1 != y2)
        {
            appendLineQuad(chunk.vertices, chunk.indices,
                           static_cast<float>(drawXi),
                           static_cast<float>(y1),
                           static_cast<float>(drawXi),
                           static_cast<float>(y2), waveformFColor);
        }
        else
        {
            appendPointQuad(chunk.vertices, chunk.indices, drawXi, y1,
                            waveformFColor);
        }

        if (connectFromPrevious)
        {
            appendLineQuad(chunk.vertices, chunk.indices,
                           static_cast<float>(prevX),
                           static_cast<float>(prevY),
                           static_cast<float>(drawXi),
                           static_cast<float>(midY), waveformFColor);
        }

        prevX = drawXi;
        prevY = midY;
        hasPrev = true;
        lastDrawXi = drawXi;
    }

    if (isCanceled())
    {
        return;
    }
    publishChunk();
}

void Waveform::ensureBackgroundBlockRenderWorker() const
{
    if (backgroundBlockRenderWorker)
    {
        return;
    }

    backgroundBlockRenderWorker =
        std::make_unique<BackgroundBlockRenderWorker>(
            [this](const BackgroundBlockRenderRequest &request,
                   const std::uint64_t generation,
                   const BackgroundBlockRenderWorker::CancelCheck &isCanceled,
                   const BackgroundBlockRenderWorker::PublishFn &publish)
            {
                processBackgroundBlockRenderRequest(request, generation,
                                                    isCanceled, publish);
            });
}

void Waveform::requestBackgroundBlockRenderPlan(
    const BaseTextureCacheKey &targetKey) const
{
    if (targetKey.samplesPerPixel < 1.0 || targetKey.width <= 0 ||
        targetKey.height <= 0)
    {
        return;
    }

    ensureBackgroundBlockRenderWorker();
    if (requestedBackgroundBlockRenderKey.has_value() &&
        *requestedBackgroundBlockRenderKey == targetKey)
    {
        return;
    }
    auto request = captureBackgroundBlockRenderRequest(targetKey);
    if (!request.has_value())
    {
        return;
    }
    requestedBackgroundBlockRenderKey = targetKey;
    backgroundBlockRenderProgress.reset();
    latestBackgroundBlockRenderGeneration =
        backgroundBlockRenderWorker->submit(std::move(*request));
}

bool Waveform::consumePublishedBackgroundBlockRenderChunks() const
{
    if (!backgroundBlockRenderWorker)
    {
        return false;
    }

    bool consumedAny = false;
    auto published = backgroundBlockRenderWorker->takePublished();
    for (auto &publishedChunk : published)
    {
        if (publishedChunk.generation != latestBackgroundBlockRenderGeneration)
        {
            continue;
        }

        auto &chunk = publishedChunk.result;
        if (!backgroundBlockRenderProgress.has_value() || chunk.reset ||
            backgroundBlockRenderProgress->generation != publishedChunk.generation ||
            backgroundBlockRenderProgress->key != chunk.key)
        {
            backgroundBlockRenderProgress = BackgroundBlockRenderProgress{
                .generation = publishedChunk.generation,
                .key = chunk.key,
                .vertices = {},
                .indices = {},
            };
        }

        auto &progress = *backgroundBlockRenderProgress;
        const int baseIndex = static_cast<int>(progress.vertices.size());
        progress.vertices.insert(progress.vertices.end(),
                                 std::make_move_iterator(chunk.vertices.begin()),
                                 std::make_move_iterator(chunk.vertices.end()));
        for (const int index : chunk.indices)
        {
            progress.indices.push_back(baseIndex + index);
        }
        consumedAny = true;
    }
    return consumedAny;
}

void Waveform::finalizeBaseTextureForView(
    const BaseTextureCacheKey &newKey, const BaseTextureCacheKey &targetKey,
    const bool allowBlockCoverageReuse) const
{
    cachedBaseTextureKey = targetKey;
    cachedBaseTextureValid = true;
    if (allowBlockCoverageReuse && targetKey != newKey)
    {
        canRenderCurrentViewFromCachedBlockTexture(newKey, targetKey,
                                                   cachedBaseTextureSourceRect);
        return;
    }

    cachedBaseTextureSourceRect = {
        0.0f, 0.0f, static_cast<float>(newKey.width),
        static_cast<float>(newKey.height)};
}

void Waveform::drawBaseWaveformContents(SDL_Renderer *renderer) const
{
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderFillRect(renderer, NULL);

    drawHorizontalLines(renderer);

    const auto &viewState = state->getActiveViewState();
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
    const bool allowBlockCoverageReuse = isBlockMode;
    if (allowBlockCoverageReuse && cachedBaseTextureValid && cachedBaseTexture)
    {
        SDL_FRect sourceRect{};
        if (canRenderCurrentViewFromCachedBlockTexture(newKey, cachedBaseTextureKey,
                                                       sourceRect))
        {
            cachedBaseTextureSourceRect = sourceRect;
            return true;
        }
    }
    if (allowBlockCoverageReuse && activateStoredBlockTextureForView(newKey))
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
    if (isBlockMode && cachedBaseTextureValid && cachedBaseTexture &&
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
        chooseBaseTextureTargetKey(newKey, allowBlockCoverageReuse);
    if (isBlockMode)
    {
        consumePublishedBackgroundBlockRenderChunks();
        if (backgroundBlockRenderProgress.has_value() &&
            backgroundBlockRenderProgress->generation ==
                latestBackgroundBlockRenderGeneration &&
            backgroundBlockRenderProgress->key == targetKey)
        {
            if (!ensureBaseTextureStorage(renderer, targetKey))
            {
                return false;
            }

            renderBaseTextureFromBackgroundPlan(
                renderer, *backgroundBlockRenderProgress);
            finalizeBaseTextureForView(newKey, targetKey,
                                       allowBlockCoverageReuse);
            return true;
        }

        requestBackgroundBlockRenderPlan(targetKey);
        if (cachedBaseTextureValid && cachedBaseTexture)
        {
            cachedBaseTextureSourceRect = {
                0.0f, 0.0f,
                static_cast<float>(cachedBaseTextureKey.width),
                static_cast<float>(cachedBaseTextureKey.height)};
            return true;
        }
    }

    if (!ensureBaseTextureStorage(renderer, targetKey))
    {
        return false;
    }

    renderBaseTexture(renderer, targetKey, isBlockMode);
    finalizeBaseTextureForView(newKey, targetKey, allowBlockCoverageReuse);
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

    destroyTexture(cachedBaseTexture);
    cachedBaseTexture = shiftedTexture;
    return true;
}

void Waveform::destroyTexture(SDL_Texture *&texture) const
{
    if (!texture)
    {
        return;
    }
    if (window && window->getRenderer() &&
        SDL_GetRenderTarget(window->getRenderer()) == texture)
    {
        SDL_SetRenderTarget(window->getRenderer(), nullptr);
    }
    SDL_DestroyTexture(texture);
    texture = nullptr;
}

void Waveform::storeCurrentBlockTexture() const
{
    if (!cachedBaseTexture || !cachedBaseTextureValid ||
        cachedBaseTextureKey.samplesPerPixel < 1.0)
    {
        return;
    }

    for (auto it = storedBlockTextures.begin(); it != storedBlockTextures.end();)
    {
        if (it->key == cachedBaseTextureKey)
        {
            destroyTexture(it->texture);
            it = storedBlockTextures.erase(it);
        }
        else
        {
            ++it;
        }
    }

    storedBlockTextures.push_back(
        {.texture = cachedBaseTexture, .key = cachedBaseTextureKey, .valid = true});
    cachedBaseTexture = nullptr;
    cachedBaseTextureValid = false;
    trimStoredBlockTextures();
}

void Waveform::trimStoredBlockTextures() const
{
    constexpr std::size_t kMaxStoredBlockTextures = 24;
    while (storedBlockTextures.size() > kMaxStoredBlockTextures)
    {
        destroyTexture(storedBlockTextures.front().texture);
        storedBlockTextures.erase(storedBlockTextures.begin());
    }
}

bool Waveform::activateStoredBlockTextureForView(
    const BaseTextureCacheKey &newKey) const
{
    SDL_FRect sourceRect{};
    for (std::size_t i = storedBlockTextures.size(); i > 0; --i)
    {
        auto &stored = storedBlockTextures[i - 1];
        if (!stored.texture || !stored.valid)
        {
            continue;
        }

        const bool exactMatch = stored.key == newKey;
        const bool reusable =
            exactMatch ||
            canRenderCurrentViewFromCachedBlockTexture(newKey, stored.key,
                                                       sourceRect);
        if (!reusable)
        {
            continue;
        }

        SDL_Texture *selectedTexture = stored.texture;
        const BaseTextureCacheKey selectedKey = stored.key;
        const bool selectedValid = stored.valid;
        const SDL_FRect selectedSourceRect =
            exactMatch
                ? SDL_FRect{0.0f, 0.0f, static_cast<float>(newKey.width),
                            static_cast<float>(newKey.height)}
                : sourceRect;
        stored.texture = nullptr;
        stored.valid = false;
        storedBlockTextures.erase(storedBlockTextures.begin() +
                                  static_cast<std::ptrdiff_t>(i - 1));

        storeCurrentBlockTexture();
        cachedBaseTexture = selectedTexture;
        cachedBaseTextureKey = selectedKey;
        cachedBaseTextureValid = selectedValid;
        cachedBaseTextureSourceRect = selectedSourceRect;
        return true;
    }

    return false;
}

void Waveform::updateSamplePoints()
{
    removeAllChildren();
    const auto &viewState = state->getActiveViewState();
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

bool Waveform::computeBlockModeSelectionFillRect(
    const int64_t firstSample, const int64_t lastSampleExclusive,
    const int64_t sampleOffset, const double samplesPerPixel, const int width,
    const int height, SDL_FRect &outRect)
{
    if (samplesPerPixel <= 0.0 || width <= 0 || height <= 0 ||
        lastSampleExclusive <= firstSample)
    {
        return false;
    }

    const double startPxD =
        (static_cast<double>(firstSample) - static_cast<double>(sampleOffset)) /
        samplesPerPixel;
    const double endPxD =
        (static_cast<double>(lastSampleExclusive) -
         static_cast<double>(sampleOffset)) /
        samplesPerPixel;

    const int startPx = static_cast<int>(std::ceil(startPxD));
    const int endPx = static_cast<int>(std::floor(endPxD));

    if (endPx <= 0 || startPx >= width)
    {
        return false;
    }

    const int drawStart = std::clamp(startPx, 0, width);
    const int drawEnd = std::clamp(endPx, 0, width);
    if (drawEnd <= drawStart)
    {
        return false;
    }

    outRect = {static_cast<float>(drawStart), 0.0f,
               static_cast<float>(drawEnd - drawStart),
               static_cast<float>(height)};
    return true;
}

bool Waveform::computeBlockModeSelectionFillEdgePixels(
    const int64_t firstSample, const int64_t lastSampleExclusive,
    const int64_t sampleOffset, const double samplesPerPixel, const int width,
    int32_t &outStartEdgePx, int32_t &outEndEdgePxExclusive)
{
    SDL_FRect rect{};
    if (!computeBlockModeSelectionFillRect(firstSample, lastSampleExclusive,
                                           sampleOffset, samplesPerPixel,
                                           width, 1, rect))
    {
        return false;
    }

    outStartEdgePx = static_cast<int32_t>(std::lround(rect.x));
    outEndEdgePxExclusive =
        static_cast<int32_t>(std::lround(rect.x + rect.w));
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
    if (!hasRenderableChannel())
    {
        return {};
    }

    const auto &session = state->getActiveDocumentSession();
    const auto &doc = session.document;
    const auto &viewState = state->getActiveViewState();
    const auto sampleData =
        doc.getAudioBuffer()->getImmutableChannelData(channelIndex);
    const auto plannedPoints = planWaveformSamplePoints(
        getWidth(), getHeight(), viewState.samplesPerPixel,
        viewState.sampleOffset, state->pixelScale, viewState.verticalZoom,
        doc.getFrameCount(),
        [&](const int64_t sampleIndex)
        {
            return sampleData[static_cast<std::size_t>(sampleIndex)];
        },
        state->uiScale);

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
    const auto samplePointSize =
        getWaveformSamplePointSize(state->pixelScale, state->uiScale);
    const auto &viewState = state->getActiveViewState();
    const auto verticalZoom = viewState.verticalZoom;
    SDL_Rect viewport{};
    SDL_GetRenderViewport(renderer, &viewport);
    const int drawWidth = std::max(0, viewport.w);

    const uint16_t yCenter = getHeight() / 2;

    SDL_SetRenderDrawColor(renderer, 100, 100, 100, 255);
    SDL_RenderLine(renderer, 0, yCenter, drawWidth, yCenter);

    if (verticalZoom <= 1.0)
    {
        SDL_SetRenderDrawColor(renderer, 150, 150, 150, 255);
        const int topY = samplePointSize / 2;
        const int bottomY = getHeight() - samplePointSize / 2;

        SDL_RenderLine(renderer, 0, topY, drawWidth, topY);
        SDL_RenderLine(renderer, 0, bottomY, drawWidth, bottomY);
    }
}

bool Waveform::shouldDrawSelection() const
{
    const auto &session = state->getActiveDocumentSession();
    const auto &viewState = state->getActiveViewState();

    return session.selection.isActive() &&
           (viewState.selectedChannels == BOTH ||
            (channelIndex == 0 && viewState.selectedChannels == LEFT) ||
            (channelIndex == 1 && viewState.selectedChannels == RIGHT));
}

void Waveform::drawBlockSelection(SDL_Renderer *renderer,
                                  const int64_t firstSample,
                                  const int64_t lastSampleExclusive,
                                  const int64_t sampleOffset,
                                  const double samplesPerPixel) const
{
    SDL_FRect selectionRect{};
    if (computeBlockModeSelectionRect(firstSample, lastSampleExclusive,
                                      sampleOffset, samplesPerPixel,
                                      getWidth(), getHeight(),
                                      selectionRect))
    {
        SDL_RenderFillRect(renderer, &selectionRect);
    }
}

void Waveform::drawLinearSelection(SDL_Renderer *renderer,
                                   const bool isSelected,
                                   const int64_t firstSample,
                                   const int64_t lastSampleExclusive,
                                   const int64_t sampleOffset,
                                   const double samplesPerPixel) const
{
    const auto selectionRect = planWaveformLinearSelectionRect(
        isSelected, firstSample, lastSampleExclusive, sampleOffset,
        samplesPerPixel, getHeight());
    if (selectionRect.visible)
    {
        SDL_RenderFillRect(renderer, &selectionRect.rect);
    }
}

void Waveform::renderSmoothWaveform(SDL_Renderer *renderer) const
{
    if (!hasRenderableChannel())
    {
        return;
    }

    const auto &session = state->getActiveDocumentSession();
    const auto &doc = session.document;
    const auto &viewState = state->getActiveViewState();
    const auto samplesPerPixel = viewState.samplesPerPixel;
    const double halfSampleWidth = 0.5 / samplesPerPixel;
    const int64_t sampleOffset = viewState.sampleOffset;
    const auto sampleData =
        doc.getAudioBuffer()->getImmutableChannelData(channelIndex);
    const auto frameCount = doc.getFrameCount();
    const auto verticalZoom = viewState.verticalZoom;
    const auto widthToUse = getWidth();
    const auto heightToUse = getHeight();
    const auto samplePointSize =
        getWaveformSamplePointSize(state->pixelScale, state->uiScale);
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

    const auto smoothedY = evaluateWaveformSmoothSpline(input);
    if (smoothedY.empty())
    {
        return;
    }

    SDL_SetRenderDrawColor(renderer, waveformColor.r, waveformColor.g,
                           waveformColor.b, waveformColor.a);

    if (smoothedY.size() == 1 || input.queryX.size() == 1)
    {
        const int x = static_cast<int>(std::lround(input.queryX.front()));
        const int y = static_cast<int>(
            std::lround(heightToUse / 2.0f - smoothedY.front() * verticalZoom *
                                                  drawableHeight / 2.0f));
        SDL_RenderPoint(renderer, x, y);
        return;
    }

    const auto sampleYToScreenY = [&](const double sampleValue)
    {
        return static_cast<int>(std::lround(
            heightToUse / 2.0f -
            static_cast<float>(sampleValue) * verticalZoom * drawableHeight /
                2.0f));
    };

    std::vector<SDL_Vertex> vertices;
    std::vector<int> indices;
    vertices.reserve(smoothedY.size() * 4);
    indices.reserve(smoothedY.size() * 6);

    for (std::size_t i = 0; i + 1 < smoothedY.size() && i + 1 < input.queryX.size();
         ++i)
    {
        const float x1 = static_cast<float>(input.queryX[i]);
        const float x2 = static_cast<float>(input.queryX[i + 1]);
        const float y1 = static_cast<float>(sampleYToScreenY(smoothedY[i]));
        const float y2 = static_cast<float>(sampleYToScreenY(smoothedY[i + 1]));

        if (std::lround(x1) == std::lround(x2) &&
            std::lround(y1) == std::lround(y2))
        {
            appendPointQuad(vertices, indices, static_cast<int>(std::lround(x1)),
                            static_cast<int>(std::lround(y1)), waveformFColor);
        }
        else
        {
            appendLineQuad(vertices, indices, x1, y1, x2, y2, waveformFColor);
        }
    }

    if (!vertices.empty() && !indices.empty())
    {
        SDL_RenderGeometry(renderer, nullptr, vertices.data(),
                           static_cast<int>(vertices.size()), indices.data(),
                           static_cast<int>(indices.size()));
    }
}

void Waveform::renderBlockWaveform(SDL_Renderer *renderer) const
{
    const auto &viewState = state->getActiveViewState();
    renderBlockWaveformRange(renderer, 0, getWidth(), getWidth(),
                             viewState.sampleOffset);
}

void Waveform::renderBlockWaveformRange(SDL_Renderer *renderer, int xStart,
                                        int xEndExclusive, int widthToUse,
                                        int64_t sampleOffset) const
{
    if (!hasRenderableChannel())
    {
        return;
    }

    auto &session = state->getActiveDocumentSession();
    auto &doc = session.document;
    const auto &viewState = state->getActiveViewState();
    SDL_SetRenderDrawColor(renderer, waveformColor.r, waveformColor.g,
                           waveformColor.b, waveformColor.a);

    const double samplesPerPixel = viewState.samplesPerPixel;
    const double blockRenderPhasePx =
        getBlockRenderPhasePixels(sampleOffset, samplesPerPixel);
    const double verticalZoom = viewState.verticalZoom;
    const int heightToUse = getHeight();
    const uint16_t samplePointSize =
        getWaveformSamplePointSize(state->pixelScale, state->uiScale);
    const auto drawableHeight = heightToUse - samplePointSize;
    const float scale = (float)(verticalZoom * drawableHeight * 0.5f);
    const int centerY = heightToUse / 2;
    xStart = std::clamp(xStart, 0, widthToUse);
    xEndExclusive = std::clamp(xEndExclusive, 0, widthToUse);
    if (xEndExclusive <= xStart)
    {
        return;
    }

    const int64_t frameCount = doc.getFrameCount();

    auto getPeakForPixel = [&](const int x, const int drawXi,
                               Peak &out) -> bool
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

        return computeWaveformPeakForSampleWindow(
            doc, channelIndex, sampleOffset, samplesPerPixel, state->pixelScale, aD,
            bD, out);
    };

    int prevX = 0;
    int prevY = 0;
    bool hasPrev = false;
    int lastDrawXi = std::numeric_limits<int>::min();
    std::vector<SDL_Vertex> vertices;
    std::vector<int> indices;
    const int visibleWidth = xEndExclusive - xStart;
    vertices.reserve(static_cast<std::size_t>(visibleWidth) * 8);
    indices.reserve(static_cast<std::size_t>(visibleWidth) * 12);

    const int lookupStart = std::max(0, xStart - 1);
    const int lookupEndExclusive = std::min(widthToUse + 1, xEndExclusive + 1);

    for (int x = lookupStart; x < lookupEndExclusive; ++x)
    {
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

        Peak p{};
        if (!getPeakForPixel(x, drawXi, p))
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
                appendLineQuad(vertices, indices, static_cast<float>(drawXi),
                               static_cast<float>(y1),
                               static_cast<float>(drawXi),
                               static_cast<float>(y2), waveformFColor);
            }
            else
            {
                appendPointQuad(vertices, indices, drawXi, y1, waveformFColor);
            }
        }

        const bool connectorTouchesVisibleRange =
            connectFromPrevious && drawXi >= xStart && drawXi < xEndExclusive;

        if (connectorTouchesVisibleRange)
        {
            appendLineQuad(vertices, indices, static_cast<float>(prevX),
                           static_cast<float>(prevY),
                           static_cast<float>(drawXi),
                           static_cast<float>(midY), waveformFColor);
        }

        prevX = drawXi;
        prevY = midY;
        hasPrev = true;
        lastDrawXi = drawXi;
    }

    if (!vertices.empty() && !indices.empty())
    {
        SDL_RenderGeometry(renderer, nullptr, vertices.data(),
                           static_cast<int>(vertices.size()), indices.data(),
                           static_cast<int>(indices.size()));
    }
}

void Waveform::drawSelection(SDL_Renderer *renderer) const
{
    const auto &session = state->getActiveDocumentSession();
    const auto &viewState = state->getActiveViewState();
    const double samplesPerPixel = viewState.samplesPerPixel;
    const bool isSelected = shouldDrawSelection();

    const int64_t firstSample = session.selection.getStartInt();
    const int64_t lastSample = session.selection.getEndExclusiveInt();
    const int64_t sampleOffset = viewState.sampleOffset;

    if (isSelected && lastSample >= sampleOffset)
    {
        SDL_SetRenderDrawColor(renderer, 0, 64, 255, 128);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

        if (samplesPerPixel >= 1.0)
        {
            drawBlockSelection(renderer, firstSample, lastSample, sampleOffset,
                               samplesPerPixel);
            return;
        }

        drawLinearSelection(renderer, isSelected, firstSample, lastSample,
                            sampleOffset, samplesPerPixel);
    }
}

std::optional<int64_t> Waveform::getHighlightedSampleIndex() const
{
    const auto window = getWindow();
    if (const auto waveformsUnderlay = dynamic_cast<WaveformsUnderlay *>(
            window ? window->getCapturingComponent() : nullptr);
        waveformsUnderlay != nullptr)
    {
        return std::nullopt;
    }

    const auto &viewState = state->getActiveViewState();
    const auto samplesPerPixel = viewState.samplesPerPixel;

    if (!shouldShowSamplePoints(samplesPerPixel, state->pixelScale) ||
        !samplePosUnderCursor.has_value())
    {
        return std::nullopt;
    }

    const auto samplePoint = dynamic_cast<SamplePoint *>(
        window ? window->getCapturingComponent() : nullptr);
    if (samplePoint != nullptr)
    {
        return samplePoint->getSampleIndex();
    }

    return *samplePosUnderCursor;
}

void Waveform::drawHighlight(SDL_Renderer *renderer) const
{
    const auto highlightedSampleIndex = getHighlightedSampleIndex();
    if (!highlightedSampleIndex.has_value())
    {
        return;
    }

    const auto &session = state->getActiveDocumentSession();
    const auto &document = session.document;
    const auto &viewState = state->getActiveViewState();
    const auto highlightRect = planWaveformHighlightRect(
        true, *highlightedSampleIndex, document.getFrameCount(),
        viewState.sampleOffset, viewState.samplesPerPixel, getHeight());
    if (highlightRect.visible)
    {
        SDL_SetRenderDrawColor(renderer, 0, 128, 255, 100);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_RenderFillRect(renderer, &highlightRect.rect);
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

    drawMarkers(renderer);

    drawCursor(renderer);

    drawPlaybackPosition(renderer);
}

void Waveform::drawPlaybackPosition(SDL_Renderer *renderer) const
{
    const auto &viewState = state->getActiveViewState();
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
    const auto &session = state->getActiveDocumentSession();
    const auto &viewState = state->getActiveViewState();
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

void Waveform::drawMarkers(SDL_Renderer *renderer) const
{
    const auto &session = state->getActiveDocumentSession();
    const auto &viewState = state->getActiveViewState();
    const auto &markers = session.document.getMarkers();

    for (const auto &marker : markers)
    {
        const auto markerPlan = planWaveformCursorMarker(
            false, marker.frame, viewState.sampleOffset, viewState.samplesPerPixel,
            getWidth());
        if (!markerPlan.visible)
        {
            continue;
        }

        const bool isSelected = viewState.selectedMarkerId == marker.id;
        if (isSelected)
        {
            SDL_SetRenderDrawColor(renderer, 255, 115, 0, 255);
        }
        else
        {
            SDL_SetRenderDrawColor(renderer, 230, 77, 26, 255);
        }

        const int yInterval = 10 * (1.f / state->pixelScale);
        for (int y = yInterval; y < getHeight(); y += yInterval)
        {
            SDL_RenderPoint(renderer, markerPlan.x, y);
        }
    }
}

void Waveform::timerCallback()
{
    if (state && state->getActiveDocumentSession().document.pumpWaveformCacheWork())
    {
        for (auto *waveform : state->waveforms)
        {
            if (!waveform)
            {
                continue;
            }
            waveform->invalidateBaseTexture();
            waveform->setDirty();
        }
    }

    if (consumePublishedBackgroundBlockRenderChunks())
    {
        setDirty();
    }

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
    auto &viewState = state->getActiveViewState();
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
