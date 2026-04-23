#pragma once

#include "../DocumentLifecycle.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>

namespace cupuacu::actions::markers
{
    struct MarkerSplitSegment
    {
        Document::AudioSegment audio;
        std::vector<DocumentMarker> markers;
    };

    inline bool splitByMarkers(State *state)
    {
        if (!state)
        {
            return false;
        }

        const auto *activeTab = state->getActiveTab();
        if (!activeTab)
        {
            return false;
        }

        const auto &document = activeTab->session.document;
        const auto &sourceMarkers = document.getMarkers();
        if (sourceMarkers.size() < 2 || document.getChannelCount() <= 0 ||
            document.getSampleRate() <= 0)
        {
            return false;
        }

        auto sortedMarkers = sourceMarkers;
        std::stable_sort(sortedMarkers.begin(), sortedMarkers.end(),
                         [](const DocumentMarker &lhs, const DocumentMarker &rhs)
                         {
                             if (lhs.frame != rhs.frame)
                             {
                                 return lhs.frame < rhs.frame;
                             }
                             return lhs.id < rhs.id;
                         });

        std::vector<MarkerSplitSegment> segments;
        segments.reserve(sortedMarkers.size() - 1);
        for (std::size_t index = 0; index + 1 < sortedMarkers.size(); ++index)
        {
            const int64_t start =
                std::clamp(sortedMarkers[index].frame, int64_t{0},
                           document.getFrameCount());
            const int64_t end =
                std::clamp(sortedMarkers[index + 1].frame, int64_t{0},
                           document.getFrameCount());
            const int64_t length = std::max<int64_t>(0, end - start);

            MarkerSplitSegment segment{};
            segment.audio = document.captureSegment(start, length);
            for (const auto &marker : sortedMarkers)
            {
                if (marker.frame < start || marker.frame > end)
                {
                    continue;
                }
                segment.markers.push_back(DocumentMarker{
                    .id = marker.id,
                    .frame = marker.frame - start,
                    .label = marker.label,
                });
            }
            segments.push_back(std::move(segment));
        }

        const int insertIndex = state->activeTabIndex + 1;
        auto tabIt = state->tabs.begin() + static_cast<std::ptrdiff_t>(insertIndex);
        for (auto &segment : segments)
        {
            DocumentTab tab{};
            tab.session.clearCurrentFile();
            tab.session.document.assignSegment(segment.audio);
            tab.session.document.replaceMarkers(std::move(segment.markers));
            tab.session.selection.reset();
            tab.session.cursor = 0;
            tab.session.syncSelectionAndCursorToDocumentLength();
            tab.viewState.selectedMarkerId.reset();
            tabIt = state->tabs.insert(tabIt, std::move(tab));
            ++tabIt;
        }

        persistSessionState(state);
        if (state->mainDocumentSessionWindow)
        {
            bindMainWindowToActiveDocument(state);
            refreshBoundDocumentUi(state);
        }
        return true;
    }
} // namespace cupuacu::actions::markers
