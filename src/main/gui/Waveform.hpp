#pragma once
#include "Component.hpp"
#include "../State.hpp"
#include "../concurrency/LatestWinsBackgroundWorker.hpp"
#include <SDL3/SDL.h>

#include "SamplePoint.hpp"

#include <memory>
#include <optional>
#include <vector>

namespace cupuacu::gui
{
    class Waveform : public Component
    {

    public:
        static bool shouldShowSamplePoints(const double samplesPerPixel,
                                           const uint8_t pixelScale);

        static void
        clearHighlightIfNotChannel(State *state,
                                   const uint8_t channelIndexNotToClear)
        {
            for (int64_t waveformChannel = 0;
                 waveformChannel < state->waveforms.size(); ++waveformChannel)
            {
                if (waveformChannel == channelIndexNotToClear)
                {
                    continue;
                }

                state->waveforms[waveformChannel]->clearHighlight();
            }
        }

        static uint16_t getWaveformWidth(const State *state)
        {
            if (state->waveforms.empty())
            {
                return 0;
            }

            return state->waveforms[0]->getWidth();
        }

        static void updateAllSamplePoints(State *state)
        {
            for (const auto &waveform : state->waveforms)
            {
                waveform->updateSamplePoints();
            }
        }

        static void setAllWaveformsDirty(State *state)
        {
            for (const auto &waveform : state->waveforms)
            {
                waveform->setDirty();
            }
        }

        static bool computeBlockModeSelectionRect(
            const int64_t firstSample, const int64_t lastSampleExclusive,
            const int64_t sampleOffset, const double samplesPerPixel,
            const int width, const int height, SDL_FRect &outRect,
            const int64_t samplesPerPeakForDisplay = 1,
            const bool includeConnectorPixelPadding = false);

        static bool computeBlockModeSelectionFillRect(
            const int64_t firstSample, const int64_t lastSampleExclusive,
            const int64_t sampleOffset, const double samplesPerPixel,
            const int width, const int height, SDL_FRect &outRect);

        static bool computeBlockModeSelectionFillEdgePixels(
            const int64_t firstSample, const int64_t lastSampleExclusive,
            const int64_t sampleOffset, const double samplesPerPixel,
            const int width, int32_t &outStartEdgePx,
            int32_t &outEndEdgePxExclusive);

        static bool computeBlockModeSelectionEdgePixels(
            const int64_t firstSample, const int64_t lastSampleExclusive,
            const int64_t sampleOffset, const double samplesPerPixel,
            const int width, int32_t &outStartEdgePx,
            int32_t &outEndEdgePxExclusive,
            const int64_t samplesPerPeakForDisplay = 1,
            const bool includeConnectorPixelPadding = false);

        static double getDoubleXPosForSampleIndex(const int64_t sampleIndex,
                                                  const int64_t sampleOffset,
                                                  const double samplesPerPixel)
        {
            return (static_cast<double>(sampleIndex) - sampleOffset) /
                   samplesPerPixel;
        }

        static int32_t getXPosForSampleIndex(const int64_t sampleIndex,
                                             const int64_t sampleOffset,
                                             const double samplesPerPixel)
        {
            return static_cast<int32_t>(
                std::roundf((static_cast<double>(sampleIndex) - sampleOffset) /
                            samplesPerPixel));
        }

        static double getDoubleSampleIndexForXPos(const float xPos,
                                                  const int64_t sampleOffset,
                                                  const double samplesPerPixel)
        {
            return static_cast<double>(xPos) * samplesPerPixel +
                   static_cast<double>(sampleOffset);
        }

        static int64_t getSampleIndexForXPos(const float xPos,
                                             const int64_t sampleOffset,
                                             const double samplesPerPixel)
        {
            return static_cast<int64_t>(
                std::llround(getDoubleSampleIndexForXPos(xPos, sampleOffset,
                                                         samplesPerPixel)));
        }

        static int64_t quantizeBlockScrollOffset(const int64_t desiredOffset,
                                                 const int64_t maxOffset,
                                                 const double samplesPerPixel,
                                                 const uint8_t pixelScale)
        {
            const int64_t clamped =
                std::clamp<int64_t>(desiredOffset, 0, std::max<int64_t>(0, maxOffset));
            if (samplesPerPixel < 1.0 ||
                shouldShowSamplePoints(samplesPerPixel, pixelScale))
            {
                return clamped;
            }

            const int64_t step = std::max<int64_t>(
                1, static_cast<int64_t>(std::llround(samplesPerPixel)));
            if (clamped <= step / 2)
            {
                return 0;
            }
            if (clamped >= maxOffset - step / 2)
            {
                return std::max<int64_t>(0, maxOffset);
            }

            return std::clamp<int64_t>(
                static_cast<int64_t>(
                    std::llround(static_cast<double>(clamped) /
                                 static_cast<double>(step))) *
                    step,
                0, std::max<int64_t>(0, maxOffset));
        }

        static double getBlockRenderSampleAnchor(const int64_t sampleOffset,
                                                 const double samplesPerPixel)
        {
            (void)samplesPerPixel;
            return static_cast<double>(sampleOffset);
        }

        static double getBlockRenderPhasePixels(const int64_t sampleOffset,
                                                const double samplesPerPixel)
        {
            (void)sampleOffset;
            (void)samplesPerPixel;
            return 0.0;
        }

        static void getBlockRenderSampleWindowForPixel(
            const int x, const int64_t sampleOffset,
            const double samplesPerPixel, double &outStartSample,
            double &outEndSample)
        {
            if (samplesPerPixel <= 0.0)
            {
                outStartSample = static_cast<double>(sampleOffset);
                outEndSample = outStartSample;
                return;
            }

            const long double spp = static_cast<long double>(samplesPerPixel);
            outStartSample = static_cast<double>(
                static_cast<long double>(sampleOffset) +
                static_cast<long double>(x) * spp);
            outEndSample = static_cast<double>(
                static_cast<long double>(sampleOffset) +
                static_cast<long double>(x + 1) * spp);
        }

        Waveform(State *, const uint8_t channelIndex);
        ~Waveform() override;

        void onDraw(SDL_Renderer *) override;
        void timerCallback() override;
        void resized() override;
        void mouseLeave() override;
        void updateSamplePoints();
        void clearHighlight();
        uint8_t getChannelIndex() const;
        void setPlaybackPosition(const int64_t newPlaybackPosition);

        std::optional<int64_t> getSamplePosUnderCursor() const;
        void setSamplePosUnderCursor(const int64_t samplePosUnderCursor);
        void resetSamplePosUnderCursor();

    private:
        const SDL_Color waveformColor{0, 185, 0, 255};
        const SDL_FColor waveformFColor{
            waveformColor.r / 255.f, waveformColor.g / 255.f,
            waveformColor.b / 255.f, waveformColor.a / 255.f};
        std::optional<int64_t> lastDrawnSamplePosUnderCursor = -1;
        std::optional<int64_t> samplePosUnderCursor;
        const uint8_t channelIndex;
        int64_t playbackPosition = -1;
        mutable std::vector<double> smoothXBuffer;
        mutable std::vector<double> smoothYBuffer;
        mutable std::vector<double> smoothQueryBuffer;
        mutable SDL_Texture *cachedBaseTexture = nullptr;

        struct BaseTextureCacheKey
        {
            int width = 0;
            int height = 0;
            int viewWidth = 0;
            int viewHeight = 0;
            int64_t sampleOffset = 0;
            int64_t frameCount = 0;
            double samplesPerPixel = 0.0;
            double verticalZoom = 0.0;
            uint64_t waveformDataVersion = 0;
            uint8_t pixelScale = 0;

            bool operator==(const BaseTextureCacheKey &other) const = default;
        };
        struct StoredBlockTexture
        {
            SDL_Texture *texture = nullptr;
            BaseTextureCacheKey key{};
            bool valid = false;
        };
        struct BackgroundBlockRenderRequest
        {
            BaseTextureCacheKey key{};
            int64_t frameCount = 0;
            float uiScale = 1.0f;
            bool bypassCache = true;
            int cacheLevel = 0;
            int64_t samplesPerPeak = 0;
            int64_t rawSampleStart = 0;
            std::vector<float> rawSamples;
            std::vector<Peak> cachedPeaks;
        };
        struct BackgroundBlockRenderChunk
        {
            BaseTextureCacheKey key{};
            bool reset = false;
            bool complete = false;
            std::vector<SDL_Vertex> vertices;
            std::vector<int> indices;
        };
        struct BackgroundBlockRenderProgress
        {
            std::uint64_t generation = 0;
            BaseTextureCacheKey key{};
            bool complete = false;
            std::vector<SDL_Vertex> vertices;
            std::vector<int> indices;
        };
        using BackgroundBlockRenderWorker =
            cupuacu::concurrency::LatestWinsBackgroundWorker<
                BackgroundBlockRenderRequest, BackgroundBlockRenderChunk>;

        mutable BaseTextureCacheKey cachedBaseTextureKey{};
        mutable bool cachedBaseTextureValid = false;
        mutable SDL_FRect cachedBaseTextureSourceRect{
            0.0f, 0.0f, 0.0f, 0.0f};
        mutable int64_t cachedBaseTextureBuiltSamplePrefixEnd = -1;
        mutable bool progressiveBlockTextureRefreshPending = false;
        mutable std::optional<BaseTextureCacheKey>
            progressiveBlockBuildGeometryKey;
        mutable int64_t progressiveBlockBuildSamplePrefixEnd = -1;
        mutable std::vector<SDL_Vertex> progressiveBlockBuildVertices;
        mutable std::vector<int> progressiveBlockBuildIndices;
        mutable std::vector<StoredBlockTexture> storedBlockTextures;
        mutable std::unique_ptr<BackgroundBlockRenderWorker>
            backgroundBlockRenderWorker;
        mutable std::optional<BaseTextureCacheKey>
            requestedBackgroundBlockRenderKey;
        mutable std::optional<BackgroundBlockRenderProgress>
            backgroundBlockRenderProgress;
        mutable std::uint64_t latestBackgroundBlockRenderGeneration = 0;

        std::vector<std::unique_ptr<SamplePoint>> computeSamplePoints();

        bool hasRenderableChannel() const;
        void invalidateBaseTexture() const;
        bool ensureBaseTexture(SDL_Renderer *) const;
        BaseTextureCacheKey computeBaseTextureCacheKey() const;
        BaseTextureCacheKey
        makeCurrentBlockTextureCoverageKey(const BaseTextureCacheKey &) const;
        BaseTextureCacheKey chooseBaseTextureTargetKey(
            const BaseTextureCacheKey &, bool allowBlockCoverageReuse) const;
        bool canRenderCurrentViewFromCachedBlockTexture(
            const BaseTextureCacheKey &currentViewKey,
            const BaseTextureCacheKey &sourceTextureKey,
            SDL_FRect &outSourceRect) const;
        bool ensureBaseTextureStorage(SDL_Renderer *,
                                      const BaseTextureCacheKey &) const;
        void renderBaseTexture(SDL_Renderer *,
                               const BaseTextureCacheKey &targetKey,
                               bool isBlockMode) const;
        bool isWaveformCacheBuildActive() const;
        int64_t currentBuiltSamplePrefixEnd() const;
        bool hasProgressiveBlockBuildGeometryForKey(
            const BaseTextureCacheKey &key) const;
        void rememberRenderedBlockTextureFrontier(
            const BaseTextureCacheKey &targetKey) const;
        bool refreshProgressiveBlockTexture(
            SDL_Renderer *renderer,
            const BaseTextureCacheKey &targetKey) const;
        void handleWaveformCacheUpdate() const;
        void renderBaseTextureFromBackgroundPlan(
            SDL_Renderer *, const BackgroundBlockRenderProgress &plan) const;
        void finalizeBaseTextureForView(const BaseTextureCacheKey &newKey,
                                        const BaseTextureCacheKey &targetKey,
                                        bool allowBlockCoverageReuse) const;
        bool canReuseBlockTextureForHorizontalShift(
            const BaseTextureCacheKey &newKey, int &outPixelShift) const;
        bool rebuildShiftedBlockTexture(SDL_Renderer *,
                                        const BaseTextureCacheKey &newKey,
                                        int pixelShift) const;
        void destroyTexture(SDL_Texture *&) const;
        void storeCurrentBlockTexture() const;
        void trimStoredBlockTextures() const;
        bool activateStoredBlockTextureForView(
            const BaseTextureCacheKey &newKey) const;
        void drawBaseWaveformContents(SDL_Renderer *) const;
        void drawProgressiveBlockBuildWaveform(
            SDL_Renderer *, const BaseTextureCacheKey &key) const;
        bool promoteProgressiveBlockBuildGeometryToTexture(
            SDL_Renderer *, const BaseTextureCacheKey &key) const;
        void appendBlockWaveformGeometryRange(
            std::vector<SDL_Vertex> &vertices, std::vector<int> &indices,
            int xStart, int xEndExclusive, int widthToUse,
            int64_t sampleOffset) const;
        void clearProgressiveBlockBuildGeometry() const;
        void drawHorizontalLines(SDL_Renderer *) const;
        bool shouldDrawSelection() const;
        void drawBlockSelection(SDL_Renderer *, int64_t firstSample,
                                int64_t lastSampleExclusive,
                                int64_t sampleOffset,
                                double samplesPerPixel) const;
        void drawLinearSelection(SDL_Renderer *, bool isSelected,
                                 int64_t firstSample,
                                 int64_t lastSampleExclusive,
                                 int64_t sampleOffset,
                                 double samplesPerPixel) const;
        void drawSelection(SDL_Renderer *) const;
        std::optional<int64_t> getHighlightedSampleIndex() const;
        void drawHighlight(SDL_Renderer *) const;
        void renderBlockWaveform(SDL_Renderer *) const;
        void renderBlockWaveformRange(SDL_Renderer *, int xStart,
                                      int xEndExclusive, int widthToUse,
                                      int64_t sampleOffset) const;
        void renderSmoothWaveform(SDL_Renderer *) const;

        void drawPlaybackPosition(SDL_Renderer *) const;
        void drawMarkers(SDL_Renderer *) const;
        void drawCursor(SDL_Renderer *) const;
        std::optional<BackgroundBlockRenderRequest>
        captureBackgroundBlockRenderRequest(
            const BaseTextureCacheKey &targetKey) const;
        void processBackgroundBlockRenderRequest(
            const BackgroundBlockRenderRequest &request,
            std::uint64_t generation,
            const BackgroundBlockRenderWorker::CancelCheck &isCanceled,
            const BackgroundBlockRenderWorker::PublishFn &publish) const;
        void ensureBackgroundBlockRenderWorker() const;
        void requestBackgroundBlockRenderPlan(
            const BaseTextureCacheKey &targetKey) const;
        bool consumePublishedBackgroundBlockRenderChunks() const;
    };
} // namespace cupuacu::gui
