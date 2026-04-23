#pragma once

#include "../Document.hpp"
#include "../State.hpp"
#include "Waveform.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

namespace cupuacu::gui
{
    enum class SnapSelectionEdge
    {
        Start,
        End,
    };

    struct VisibleFrameBounds
    {
        int64_t start = 0;
        int64_t endExclusive = 0;
    };

    inline VisibleFrameBounds planVisibleFrameBounds(const int64_t frameCount,
                                                     const int64_t sampleOffset,
                                                     const double samplesPerPixel,
                                                     const int waveformWidth)
    {
        if (frameCount <= 0 || samplesPerPixel <= 0.0 || waveformWidth <= 0)
        {
            return {};
        }

        const int64_t start = std::clamp(sampleOffset, int64_t{0}, frameCount);
        const int64_t endExclusive = std::clamp<int64_t>(
            static_cast<int64_t>(std::ceil(
                static_cast<double>(sampleOffset) +
                static_cast<double>(waveformWidth) * samplesPerPixel)),
            int64_t{0}, frameCount);
        return {.start = start, .endExclusive = endExclusive};
    }

    inline int64_t snapSamplePosition(
        const State *state, const int64_t rawFrame,
        const std::optional<uint64_t> movingMarkerId, const bool includeCursor,
        const std::optional<SnapSelectionEdge> movingSelectionEdge,
        const int64_t sampleOffset, const double samplesPerPixel,
        const int waveformWidth, const float pixelThreshold = 8.0f)
    {
        if (!state || !state->snapEnabled || samplesPerPixel <= 0.0 ||
            waveformWidth <= 0)
        {
            return rawFrame;
        }

        const auto &session = state->getActiveDocumentSession();
        const auto &document = session.document;
        const int64_t frameCount = document.getFrameCount();
        const auto visibleBounds = planVisibleFrameBounds(
            frameCount, sampleOffset, samplesPerPixel, waveformWidth);

        std::vector<int64_t> snapTargets = {
            int64_t{0},
            frameCount,
            visibleBounds.start,
            visibleBounds.endExclusive,
        };

        if (includeCursor)
        {
            snapTargets.push_back(std::clamp(session.cursor, int64_t{0}, frameCount));
        }

        if (session.selection.isActive())
        {
            if (movingSelectionEdge != SnapSelectionEdge::Start)
            {
                snapTargets.push_back(std::clamp(session.selection.getStartInt(),
                                                 int64_t{0}, frameCount));
            }
            if (movingSelectionEdge != SnapSelectionEdge::End)
            {
                snapTargets.push_back(
                    std::clamp(session.selection.getEndExclusiveInt(),
                               int64_t{0}, frameCount));
            }
        }

        for (const auto &marker : document.getMarkers())
        {
            if (movingMarkerId.has_value() && marker.id == *movingMarkerId)
            {
                continue;
            }
            snapTargets.push_back(std::clamp(marker.frame, int64_t{0}, frameCount));
        }

        const double rawX = Waveform::getDoubleXPosForSampleIndex(
            rawFrame, sampleOffset, samplesPerPixel);
        double bestDistancePx = std::numeric_limits<double>::infinity();
        int64_t bestFrame = rawFrame;
        for (const auto targetFrame : snapTargets)
        {
            const double targetX = Waveform::getDoubleXPosForSampleIndex(
                targetFrame, sampleOffset, samplesPerPixel);
            const double distancePx = std::abs(targetX - rawX);
            if (distancePx > pixelThreshold)
            {
                continue;
            }
            if (distancePx < bestDistancePx ||
                (std::abs(distancePx - bestDistancePx) <= 1e-6 &&
                 targetFrame < bestFrame))
            {
                bestDistancePx = distancePx;
                bestFrame = targetFrame;
            }
        }

        return std::clamp(bestFrame, int64_t{0}, frameCount);
    }

    inline int64_t planSnappedMouseSamplePosition(
        const State *state, const float mouseX, const bool includeCursor,
        const std::optional<SnapSelectionEdge> movingSelectionEdge =
            std::nullopt)
    {
        if (!state)
        {
            return 0;
        }

        const auto &session = state->getActiveDocumentSession();
        const auto &viewState = state->getActiveViewState();
        const int64_t rawFrame = std::clamp(
            static_cast<int64_t>(std::llround(Waveform::getDoubleSampleIndexForXPos(
                mouseX, viewState.sampleOffset, viewState.samplesPerPixel))),
            int64_t{0}, session.document.getFrameCount());
        return snapSamplePosition(
            state, rawFrame, std::nullopt, includeCursor, movingSelectionEdge,
            viewState.sampleOffset, viewState.samplesPerPixel,
            Waveform::getWaveformWidth(state));
    }

    inline bool planSnappedVisibleRangeSelection(
        const State *state, const float mouseX, int64_t &startOut,
        int64_t &endExclusiveOut)
    {
        if (!state)
        {
            return false;
        }

        const auto &session = state->getActiveDocumentSession();
        const auto &viewState = state->getActiveViewState();
        const auto &document = session.document;
        const auto visibleBounds = planVisibleFrameBounds(
            document.getFrameCount(), viewState.sampleOffset,
            viewState.samplesPerPixel, Waveform::getWaveformWidth(state));
        if (visibleBounds.endExclusive <= visibleBounds.start)
        {
            return false;
        }

        startOut = visibleBounds.start;
        endExclusiveOut = visibleBounds.endExclusive;
        if (!state->snapEnabled)
        {
            return true;
        }

        const double mouseFrame = Waveform::getDoubleSampleIndexForXPos(
            mouseX, viewState.sampleOffset, viewState.samplesPerPixel);
        std::optional<int64_t> leftMarkerFrame;
        std::optional<int64_t> rightMarkerFrame;
        for (const auto &marker : document.getMarkers())
        {
            if (marker.frame < visibleBounds.start ||
                marker.frame > visibleBounds.endExclusive)
            {
                continue;
            }

            if (marker.frame <= mouseFrame &&
                (!leftMarkerFrame.has_value() || marker.frame > *leftMarkerFrame))
            {
                leftMarkerFrame = marker.frame;
            }
            if (marker.frame >= mouseFrame &&
                (!rightMarkerFrame.has_value() || marker.frame < *rightMarkerFrame))
            {
                rightMarkerFrame = marker.frame;
            }
        }

        if (leftMarkerFrame.has_value())
        {
            startOut = std::max(startOut, *leftMarkerFrame);
        }
        if (rightMarkerFrame.has_value())
        {
            endExclusiveOut = std::min(endExclusiveOut, *rightMarkerFrame);
        }
        return endExclusiveOut > startOut;
    }
} // namespace cupuacu::gui
