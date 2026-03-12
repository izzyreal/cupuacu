#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <SDL3/SDL.h>

namespace cupuacu::gui
{
    struct WaveformLineMarker
    {
        bool visible = false;
        int32_t x = 0;
    };

    struct WaveformRectMarker
    {
        bool visible = false;
        SDL_FRect rect{0.0f, 0.0f, 0.0f, 0.0f};
    };

    inline bool shouldRenderWaveformSamplePoints(const int64_t playbackPosition,
                                                 const double samplesPerPixel,
                                                 const uint8_t pixelScale)
    {
        return playbackPosition < 0 &&
               samplesPerPixel < static_cast<float>(pixelScale) / 40.0f;
    }

    inline WaveformLineMarker planWaveformPlaybackMarker(
        const int64_t playbackPosition, const int64_t sampleOffset,
        const double samplesPerPixel, const int width)
    {
        WaveformLineMarker marker{};
        if (playbackPosition < 0 || samplesPerPixel <= 0.0 || width < 0)
        {
            return marker;
        }

        marker.x = static_cast<int32_t>(std::round(
            (static_cast<double>(playbackPosition) - sampleOffset) /
            samplesPerPixel));
        marker.visible = marker.x >= 0 && marker.x <= width;
        return marker;
    }

    inline WaveformLineMarker planWaveformCursorMarker(
        const bool selectionActive, const int64_t cursor,
        const int64_t sampleOffset, const double samplesPerPixel,
        const int width)
    {
        WaveformLineMarker marker{};
        if (selectionActive || samplesPerPixel <= 0.0 || width < 0)
        {
            return marker;
        }

        marker.x = static_cast<int32_t>(std::round(
            (static_cast<double>(cursor) - sampleOffset) / samplesPerPixel));
        marker.visible = marker.x >= 0 && marker.x <= width;
        return marker;
    }

    inline WaveformRectMarker planWaveformHighlightRect(
        const bool samplePointsVisible, const int64_t sampleIndex,
        const int64_t frameCount, const int64_t sampleOffset,
        const double samplesPerPixel, const int height)
    {
        WaveformRectMarker marker{};
        if (!samplePointsVisible || sampleIndex < 0 || sampleIndex >= frameCount ||
            samplesPerPixel <= 0.0 || height <= 0)
        {
            return marker;
        }

        const float x = static_cast<float>(std::round(
            (static_cast<double>(sampleIndex) - sampleOffset) /
            samplesPerPixel));
        marker.visible = true;
        marker.rect = {x, 0.0f, static_cast<float>(1.0 / samplesPerPixel),
                       static_cast<float>(height)};
        return marker;
    }

    inline WaveformRectMarker planWaveformLinearSelectionRect(
        const bool selectionActive, const int64_t firstSample,
        const int64_t lastSampleExclusive, const int64_t sampleOffset,
        const double samplesPerPixel, const int height)
    {
        WaveformRectMarker marker{};
        if (!selectionActive || samplesPerPixel <= 0.0 || height <= 0 ||
            lastSampleExclusive < sampleOffset)
        {
            return marker;
        }

        const float startX =
            firstSample <= sampleOffset
                ? 0.0f
                : static_cast<float>(std::round(
                      (static_cast<double>(firstSample) - sampleOffset) /
                      samplesPerPixel));
        const float endX = static_cast<float>(std::round(
            (static_cast<double>(lastSampleExclusive) - sampleOffset) /
            samplesPerPixel));
        const float width =
            std::abs(endX - startX) < 1.0f ? 1.0f : (endX - startX);

        marker.visible = true;
        marker.rect = {startX, 0.0f, width, static_cast<float>(height)};
        return marker;
    }
} // namespace cupuacu::gui
