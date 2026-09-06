#pragma once

#include "../gui/PeakLevel.hpp"
#include "../storage/AudioReader.hpp"
#include <bit>
#include <limits>

namespace cupuacu::storage
{
    class RevisionArchive;
}

namespace cupuacu::waveform
{
    using Peak = gui::Peak;
    inline Peak emptyPeak()
    {
        return {std::numeric_limits<float>::infinity(),
                -std::numeric_limits<float>::infinity()};
    }
    inline Peak combine(Peak a, Peak b)
    {
        return {std::min(a.min, b.min), std::max(a.max, b.max)};
    }

    // Immutable source-coordinate summaries. Existing peak pages are shared;
    // only levels beyond the legacy ceiling are added, up to a full overview.
    class SourcePeaks
    {
        friend class storage::RevisionArchive;
        storage::AudioShape dimensions;
        std::vector<std::vector<gui::PeakLevel>> channels;

    public:
        static constexpr int64_t blockFrames = 128;
        SourcePeaks(storage::AudioShape shape,
                    std::vector<std::vector<gui::PeakLevel>> levels)
            : dimensions(shape), channels(std::move(levels))
        {
            if (shape.frames < 0 || shape.channels <= 0 ||
                int(channels.size()) != shape.channels)
            {
                throw std::invalid_argument("Invalid source peak shape");
            }
            for (auto &channel : channels)
            {
                std::size_t expected = shape.frames / blockFrames +
                                       (shape.frames % blockFrames != 0);
                if (channel.empty())
                {
                    throw std::invalid_argument("Missing source peak levels");
                }
                for (const auto &level : channel)
                {
                    if (level.size() != expected)
                    {
                        throw std::invalid_argument(
                            "Invalid source peak level length");
                    }
                    expected = expected / 2 + expected % 2;
                }
                while (channel.back().size() > 1)
                {
                    const auto &previous = channel.back();
                    gui::PeakLevel next;
                    next.resize(previous.size() / 2 + previous.size() % 2);
                    for (std::size_t i = 0; i < next.size(); ++i)
                    {
                        next.set(i, i * 2 + 1 < previous.size()
                                        ? combine(previous[i * 2],
                                                  previous[i * 2 + 1])
                                        : previous[i * 2]);
                    }
                    channel.push_back(std::move(next));
                }
            }
        }
        storage::AudioShape shape() const
        {
            return dimensions;
        }
        Peak queryBlocks(int channel, int64_t first, int64_t end,
                         uint64_t &visited) const
        {
            if (channel < 0 || channel >= dimensions.channels || first < 0 ||
                end < first || uint64_t(end) > channels[channel][0].size())
            {
                throw std::out_of_range("Source peak query outside revision");
            }
            auto result = emptyPeak();
            while (first < end)
            {
                const auto remaining = uint64_t(end - first);
                const int level = std::min(
                    int(std::bit_width(remaining)) - 1,
                    first ? int(std::countr_zero(uint64_t(first))) : 63);
                result = combine(
                    result, channels[channel][level][uint64_t(first) >> level]);
                first += int64_t(1) << level;
                ++visited;
            }
            return result;
        }
    };
} // namespace cupuacu::waveform
