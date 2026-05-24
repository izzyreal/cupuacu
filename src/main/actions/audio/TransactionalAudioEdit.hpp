#pragma once

#include "../../DocumentSession.hpp"
#include "../../LongTask.hpp"
#include "../../gui/MainViewAccess.hpp"
#include "../../gui/WaveformRefresh.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>

namespace cupuacu::actions::audio::detail
{
    class OperationProgressUi
    {
    public:
        OperationProgressUi(State *stateToUse, std::string initialDetail)
            : state(stateToUse),
              lastProgressDetail(std::move(initialDetail)),
              lastProgressRenderedAt(std::chrono::steady_clock::now())
        {
        }

        void publishProgress(const std::string &detail, const double progress,
                             const bool forceRender)
        {
            const auto now = std::chrono::steady_clock::now();
            const bool shouldRender =
                forceRender || detail != lastProgressDetail || progress >= 1.0 ||
                now - lastProgressRenderedAt >= kProgressUiThrottle;

            lastProgressDetail = detail;
            cupuacu::updateLongTaskOverlayOnly(state, detail, progress, false);
            if (shouldRender)
            {
                cupuacu::renderLongTaskOverlayNow(state);
                lastProgressRenderedAt = now;
            }
        }

        void publishPhaseProgress(const std::string &detail,
                                  const double startProgress,
                                  const double endProgress,
                                  const int64_t completed,
                                  const int64_t total)
        {
            const auto safeTotal = std::max<int64_t>(1, total);
            const double normalized =
                std::clamp(static_cast<double>(completed) /
                               static_cast<double>(safeTotal),
                           0.0, 1.0);
            publishProgress(detail,
                            startProgress +
                                (endProgress - startProgress) * normalized,
                            normalized >= 1.0);
        }

    private:
        static constexpr auto kProgressUiThrottle =
            std::chrono::milliseconds(50);

        State *state = nullptr;
        std::string lastProgressDetail;
        std::chrono::steady_clock::time_point lastProgressRenderedAt;
    };

    inline void publishCancelablePhaseProgress(
        State *state, OperationProgressUi &progressUi,
        const std::string &detail, const double startProgress,
        const double endProgress, const int64_t completed, const int64_t total)
    {
        progressUi.publishPhaseProgress(detail, startProgress, endProgress,
                                        completed, total);
        cupuacu::throwIfLongTaskCanceled(state);
    }

    inline void writeSegmentWithCancelableProgress(
        State *state, cupuacu::Document &document, const int64_t startFrame,
        const cupuacu::Document::AudioSegment &segment,
        OperationProgressUi &progressUi, const std::string &detail,
        const double startProgress, const double endProgress)
    {
        if (segment.frameCount <= 0 || segment.channelCount <= 0)
        {
            progressUi.publishProgress(detail, endProgress, true);
            return;
        }

        document.writeSegment(
            startFrame, segment, false,
            [&](const int64_t completed, const int64_t total)
            {
                publishCancelablePhaseProgress(
                    state, progressUi, detail, startProgress, endProgress,
                    completed, total);
            });
    }

    inline void rebuildWaveformCacheAfterTransactionalCommit(
        State *state, cupuacu::DocumentSession &session,
        OperationProgressUi &progressUi, const std::string &completedDetail)
    {
        session.waveformCaches.resetToChannelCount(
            session.document.getChannelCount());
        progressUi.publishProgress("Updating waveform", 0.95, true);
        session.updateWaveformCache();
        while (true)
        {
            const auto buildProgress = session.getWaveformCacheBuildProgress();
            if (!buildProgress.has_value())
            {
                break;
            }
            progressUi.publishPhaseProgress(
                "Updating waveform", 0.95, 1.0,
                buildProgress->completedBlocks,
                std::max<int64_t>(1, buildProgress->totalBlocks));
            if (!session.pumpWaveformCacheWork())
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
        cupuacu::gui::Waveform::invalidateAllRenderingCaches(state);
        cupuacu::gui::refreshWaveforms(state, true, true);
        cupuacu::gui::requestMainViewRefresh(state);
        progressUi.publishProgress(completedDetail, 1.0, true);
    }
} // namespace cupuacu::actions::audio::detail
