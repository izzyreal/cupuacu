#include "PeakAnalysis.hpp"
#include <array>

namespace cupuacu::effects
{
    PeakAnalysis::PeakAnalysis(
        std::shared_ptr<const storage::AudioReader> reader,
        std::shared_ptr<const storage::AudioEditRevision> revision,
        std::shared_ptr<const waveform::ViewportSource> view)
        : worker(
              [reader = std::move(reader), revision = std::move(revision),
               view = std::move(view)](const PeakAnalysisRequest &request,
                                       const Worker::CancelCheck &cancel)
              {
                  if (!reader)
                  {
                      throw std::invalid_argument(
                          "Missing peak analysis reader");
                  }
                  return compute(*reader, revision.get(), view.get(), request,
                                 cancel);
              })
    {
    }

    std::optional<float>
    PeakAnalysis::compute(const storage::AudioReader &reader,
                          const storage::AudioEditRevision *revision,
                          const waveform::ViewportSource *view,
                          const PeakAnalysisRequest &request,
                          const Worker::CancelCheck &cancel)
    {
        if (request.count <= 0 || request.channels.empty())
        {
            throw std::invalid_argument("Peak analysis requires audio");
        }
        for (auto c : request.channels)
        {
            storage::AudioReader::validateRange(
                reader.shape(), c, request.start, std::size_t(request.count));
        }
        float peak = 0;
        auto include = [&](waveform::Peak value)
        {
            peak = std::max({peak, std::fabs(value.min), std::fabs(value.max)});
        };
        if (revision)
        {
            // Trimming makes the selected range the exact root summary. It
            // excludes out-of-selection spikes inside the boundary buckets.
            storage::AudioEditTransaction selected(*revision);
            selected.trim(request.start, request.count);
            auto audio = selected.finish();
            storage::AudioEditRevision::PeakWork work;
            if (audio->prepareWaveform(work, cancel))
            {
                for (auto c : request.channels)
                {
                    if (cancel())
                    {
                        return {};
                    }
                    const auto summary =
                        audio->queryWaveformOverview(c, 0, request.count, work);
                    if (!summary)
                    {
                        throw std::logic_error("Prepared peak is missing");
                    }
                    include(*summary);
                }
                return peak;
            }
        }
        std::array<float, 16384> samples;
        auto scan = [&](int c, int64_t first, int64_t end)
        {
            while (first < end)
            {
                if (cancel())
                {
                    return false;
                }
                auto out = std::span(samples).first(
                    std::min<int64_t>(samples.size(), end - first));
                reader.readChannel(c, first, out);
                for (auto value : out)
                {
                    peak = std::max(peak, std::fabs(value));
                }
                first += out.size();
            }
            return true;
        };
        const auto end = request.start + request.count;
        for (auto c : request.channels)
        {
            if (cancel())
            {
                return {};
            }
            const auto head = std::min(
                end, request.start + (128 - request.start % 128) % 128);
            const auto tail = end / 128 * 128;
            if (!revision && view && view->overview && tail > head)
            {
                if (const auto middle = view->overview(c, head, tail - head))
                {
                    include(*middle);
                    if (!scan(c, request.start, head) || !scan(c, tail, end))
                    {
                        return {};
                    }
                    continue;
                }
            }
            if (!scan(c, request.start, end))
            {
                return {};
            }
        }
        return peak;
    }
} // namespace cupuacu::effects
