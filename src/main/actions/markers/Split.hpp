#pragma once

#include "../MutationAvailability.hpp"
#include "../audio/RevisionEdit.hpp"
#include "../DocumentLifecycle.hpp"
#include "../../LongTask.hpp"
#include "../../storage/AudioEditRevision.hpp"

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

        if (auto source = activeTab->session.getEditRevision())
        {
            audio::prepareRevisionAction(
                state,
                [source, sortedMarkers](const auto &)
                {
                    // Build all destination tabs before inserting any. Audio
                    // and peaks remain shared with the source, including
                    // unaligned boundaries.
                    auto retained = concurrency::releaseOnWorker(
                        std::make_shared<std::vector<DocumentTab>>());
                    auto &destinations = *retained;
                    destinations.reserve(sortedMarkers.size() - 1);
                    for (std::size_t i = 0; i + 1 < sortedMarkers.size(); ++i)
                    {
                        const auto start =
                            std::clamp(sortedMarkers[i].frame, int64_t{0},
                                       source->shape().frames);
                        const auto end =
                            std::clamp(sortedMarkers[i + 1].frame, start,
                                       source->shape().frames);
                        storage::AudioEditTransaction transaction(*source);
                        transaction.trim(start, end - start);
                        auto slice = transaction.finish();
                        DocumentTab tab;
                        const auto shape = slice->shape();
                        tab.session.document.setExternalAudioShape(
                            shape.format, shape.sampleRate, shape.channels,
                            shape.frames);
                        std::vector<DocumentMarker> markers;
                        for (const auto &marker : sortedMarkers)
                        {
                            if (marker.frame >= start && marker.frame <= end)
                            {
                                markers.push_back({marker.id,
                                                   marker.frame - start,
                                                   marker.label});
                            }
                        }
                        tab.session.document.replaceMarkers(std::move(markers));
                        tab.session.bindReadRevision(std::move(slice));
                        // These are new unsaved documents, not clean copies of
                        // a file.
                        tab.session.markRevisionSaved({}, {});
                        tab.session.waveformCaches.resetToChannelCount(
                            shape.channels);
                        destinations.push_back(std::move(tab));
                    }
                    return [retained](State *state, int targetIndex)
                    {
                        auto &destinations = *retained;
                        state->tabs.insert(
                            state->tabs.begin() + targetIndex + 1,
                            std::make_move_iterator(destinations.begin()),
                            std::make_move_iterator(destinations.end()));
                        persistSessionState(state);
                        if (state->mainDocumentSessionWindow)
                        {
                            bindMainWindowToActiveDocument(state);
                            refreshBoundDocumentUi(state);
                        }
                    };
                });
            return true;
        }

        audio::prepareRevisionAction(
            state,
            [sourceDocument, sortedMarkers](const auto &)
            {
                auto retained = concurrency::releaseOnWorker(
                    std::make_shared<std::vector<DocumentTab>>());
                for (std::size_t i = 0; i + 1 < sortedMarkers.size(); ++i)
                {
                    const auto start =
                        std::clamp(sortedMarkers[i].frame, int64_t{0},
                                   sourceDocument.getFrameCount());
                    const auto end =
                        std::clamp(sortedMarkers[i + 1].frame, start,
                                   sourceDocument.getFrameCount());
                    DocumentTab tab;
                    tab.session.document.assignSegment(
                        sourceDocument.captureSegment(start, end - start));
                    std::vector<DocumentMarker> markers;
                    for (const auto &marker : sortedMarkers)
                    {
                        if (marker.frame >= start && marker.frame <= end)
                        {
                            markers.push_back({marker.id, marker.frame - start,
                                               marker.label});
                        }
                    }
                    tab.session.document.replaceMarkers(std::move(markers));
                    tab.session.waveformCaches.resetToChannelCount(
                        sourceDocument.getChannelCount());
                    tab.session.updateWaveformCache();
                    retained->push_back(std::move(tab));
                }
                return [retained](State *state, int index)
                {
                    state->tabs.insert(
                        state->tabs.begin() + index + 1,
                        std::make_move_iterator(retained->begin()),
                        std::make_move_iterator(retained->end()));
                    persistSessionState(state);
                    if (state->mainDocumentSessionWindow)
                    {
                        bindMainWindowToActiveDocument(state);
                        refreshBoundDocumentUi(state);
                    }
                };
            });
        return true;
    }
} // namespace cupuacu::actions::markers
