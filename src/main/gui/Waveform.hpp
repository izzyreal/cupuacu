#pragma once
#include "Component.hpp"
#include "../State.hpp"
#include <SDL3/SDL.h>

#include "SamplePoint.hpp"

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

        static double getBlockRenderSampleAnchor(const int64_t sampleOffset,
                                                 const double samplesPerPixel)
        {
            if (samplesPerPixel <= 0.0)
            {
                return static_cast<double>(sampleOffset);
            }
            const long double spp = static_cast<long double>(samplesPerPixel);
            const long double offset =
                static_cast<long double>(sampleOffset);
            const auto anchorIndex =
                static_cast<int64_t>(std::floor(offset / spp));
            return static_cast<double>(static_cast<long double>(anchorIndex) *
                                       spp);
        }

        static double getBlockRenderPhasePixels(const int64_t sampleOffset,
                                                const double samplesPerPixel)
        {
            if (samplesPerPixel <= 0.0)
            {
                return 0.0;
            }
            const long double spp = static_cast<long double>(samplesPerPixel);
            const long double offset =
                static_cast<long double>(sampleOffset);
            const auto anchorIndex =
                static_cast<int64_t>(std::floor(offset / spp));
            const long double phaseSamples =
                offset - static_cast<long double>(anchorIndex) * spp;
            return static_cast<double>(phaseSamples / spp);
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
            const long double offset =
                static_cast<long double>(sampleOffset);
            const auto anchorIndex =
                static_cast<int64_t>(std::floor(offset / spp));

            outStartSample = static_cast<double>(
                (static_cast<long double>(anchorIndex) +
                 static_cast<long double>(x)) *
                spp);
            outEndSample = static_cast<double>(
                (static_cast<long double>(anchorIndex) +
                 static_cast<long double>(x + 1)) *
                spp);
        }

        Waveform(State *, const uint8_t channelIndex);

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

        std::vector<std::unique_ptr<SamplePoint>> computeSamplePoints();

        void drawHorizontalLines(SDL_Renderer *) const;
        void drawSelection(SDL_Renderer *) const;
        void drawHighlight(SDL_Renderer *) const;
        void renderBlockWaveform(SDL_Renderer *) const;
        void renderSmoothWaveform(SDL_Renderer *) const;

        void drawPlaybackPosition(SDL_Renderer *) const;
        void drawCursor(SDL_Renderer *) const;
    };
} // namespace cupuacu::gui
