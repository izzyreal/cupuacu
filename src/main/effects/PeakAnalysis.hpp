#pragma once
#include "../concurrency/LatestValueWorker.hpp"
#include "../storage/AudioEditRevision.hpp"
#include "../waveform/WaveformViewport.hpp"

namespace cupuacu::effects
{
    struct PeakAnalysisRequest
    {
        int64_t start = 0, count = 0;
        std::vector<int> channels;
    };
    class PeakAnalysis
    {
        using Worker =
            concurrency::LatestValueWorker<PeakAnalysisRequest, float>;
        Worker worker;

    public:
        PeakAnalysis(
            std::shared_ptr<const storage::AudioReader> reader,
            std::shared_ptr<const storage::AudioEditRevision> revision = {},
            std::shared_ptr<const waveform::ViewportSource> view = {});
        uint64_t submit(PeakAnalysisRequest request)
        {
            return worker.submit(std::move(request));
        }
        auto takePublished()
        {
            return worker.takePublished();
        }
        void close()
        {
            worker.close();
        }
        void waitUntilClosed()
        {
            worker.waitUntilClosed();
        }
        // Exact peak: uses immutable summaries plus bounded boundary reads.
        // Cancellation and all disk access occur on the worker.
        static std::optional<float> compute(const storage::AudioReader &,
                                            const storage::AudioEditRevision *,
                                            const waveform::ViewportSource *,
                                            const PeakAnalysisRequest &,
                                            const Worker::CancelCheck &);
    };
} // namespace cupuacu::effects
