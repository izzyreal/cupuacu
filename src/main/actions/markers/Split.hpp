#pragma once

#include "../MutationAvailability.hpp"
#include "../DocumentLifecycle.hpp"
#include "../../LongTask.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <exception>
#include <optional>
#include <thread>
#include <vector>

namespace cupuacu::actions::markers
{
    struct MarkerSplitSegment
    {
        Document document;
    };

    inline bool splitByMarkers(State *state)
    {
        if (!state || !cupuacu::actions::isDocumentMutationAvailable(state))
        {
            return false;
        }

        const auto *activeTab = state->getActiveTab();
        if (!activeTab)
        {
            return false;
        }

        const Document sourceDocument = activeTab->session.document;
        const auto sourceMarkers = sourceDocument.getMarkers();
        if (sourceMarkers.size() < 2 ||
            sourceDocument.getChannelCount() <= 0 ||
            sourceDocument.getSampleRate() <= 0)
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
        std::atomic_bool completed{false};
        std::atomic_bool cancelRequested{false};
        std::atomic<std::size_t> completedSegments{0};
        std::exception_ptr workerError;
        cupuacu::LongTaskScope longTask(
            state, "Splitting by markers", "Preparing documents", 0.0, true,
            true);
        std::thread worker(
            [&]
            {
                try
                {
                    segments.reserve(sortedMarkers.size() - 1);
                    for (std::size_t index = 0;
                         index + 1 < sortedMarkers.size(); ++index)
                    {
                        if (cancelRequested.load(std::memory_order_acquire))
                        {
                            throw cupuacu::LongTaskCanceledError{};
                        }

                        const int64_t start =
                            std::clamp(sortedMarkers[index].frame, int64_t{0},
                                       sourceDocument.getFrameCount());
                        const int64_t end =
                            std::clamp(sortedMarkers[index + 1].frame,
                                       int64_t{0},
                                       sourceDocument.getFrameCount());
                        const int64_t length =
                            std::max<int64_t>(0, end - start);

                        auto audio = sourceDocument.captureSegment(
                            start, length,
                            [&](const int64_t, const int64_t)
                            {
                                if (cancelRequested.load(
                                        std::memory_order_acquire))
                                {
                                    throw cupuacu::LongTaskCanceledError{};
                                }
                            });
                        MarkerSplitSegment segment{};
                        segment.document.assignSegment(audio);
                        std::vector<DocumentMarker> segmentMarkers;
                        for (const auto &marker : sortedMarkers)
                        {
                            if (marker.frame < start || marker.frame > end)
                            {
                                continue;
                            }
                            segmentMarkers.push_back(DocumentMarker{
                                .id = marker.id,
                                .frame = marker.frame - start,
                                .label = marker.label,
                            });
                        }
                        segment.document.replaceMarkers(
                            std::move(segmentMarkers));
                        segments.push_back(std::move(segment));
                        completedSegments.store(index + 1,
                                                std::memory_order_release);
                    }
                }
                catch (...)
                {
                    workerError = std::current_exception();
                }
                completed.store(true, std::memory_order_release);
            });

        auto lastRender = std::chrono::steady_clock::now();
        while (!completed.load(std::memory_order_acquire))
        {
            if (cupuacu::isLongTaskCancelRequested(state))
            {
                cancelRequested.store(true, std::memory_order_release);
            }
            const auto now = std::chrono::steady_clock::now();
            if (now - lastRender >= std::chrono::milliseconds(50))
            {
                const auto total = sortedMarkers.size() - 1;
                cupuacu::updateLongTaskOverlayOnly(
                    state, "Preparing documents",
                    static_cast<double>(
                        completedSegments.load(std::memory_order_acquire)) /
                        static_cast<double>(std::max<std::size_t>(1, total)),
                    false);
                cupuacu::renderLongTaskOverlayNow(state);
                lastRender = now;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        worker.join();
        if (workerError)
        {
            try
            {
                std::rethrow_exception(workerError);
            }
            catch (const cupuacu::LongTaskCanceledError &)
            {
                return false;
            }
        }

        const int insertIndex = state->activeTabIndex + 1;
        auto tabIt = state->tabs.begin() + static_cast<std::ptrdiff_t>(insertIndex);
        for (auto &segment : segments)
        {
            DocumentTab tab{};
            tab.session.clearCurrentFile();
            tab.session.document = std::move(segment.document);
            tab.session.waveformCaches.resetToChannelCount(
                tab.session.document.getChannelCount());
            tab.session.updateWaveformCache();
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
