#pragma once

#include "../gui/PeakLevel.hpp"
#include "../storage/AudioReader.hpp"
#include "../storage/WorkingMemory.hpp"
#include <bit>
#include <array>
#include <functional>
#include <limits>

namespace cupuacu::storage
{
    class RevisionArchive;
    class DecodedBlockCache;
} // namespace cupuacu::storage

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
        std::shared_ptr<void> residentMemory;
        std::vector<std::vector<gui::PeakLevel>> channels;
        struct PagedData;
        std::shared_ptr<PagedData> paged;
        std::function<void(int, std::size_t, uint64_t, std::span<Peak>)>
            externalRead;
        std::function<std::array<uint64_t, 3>()> externalStats;
        SourcePeaks(storage::AudioShape shape, std::size_t levelCount)
            : dimensions(shape), channels(shape.channels,
                std::vector<gui::PeakLevel>(levelCount)) {}

    public:
        static constexpr int64_t blockFrames = 128;
        static constexpr std::size_t residentLevelLimit = 4096;
        using ReadBasePeaks = std::function<void(int, uint64_t, std::span<Peak>)>;
        static std::shared_ptr<const SourcePeaks> fromReader(
            storage::AudioShape,
            std::function<void(int, std::size_t, uint64_t, std::span<Peak>)>,
            std::function<std::array<uint64_t, 3>()>);
        // Build a pyramid one spatial tile at a time. The callback supplies
        // consecutive level-zero summaries; no full peak array is required.
        static std::shared_ptr<const SourcePeaks> createStreaming(
            storage::AudioShape, const ReadBasePeaks &,
            std::shared_ptr<storage::DecodedBlockCache> cache = {},
            const std::function<bool()> &cancel = {});
        // Worker-only: retain the overview and move detailed levels to owned
        // temporary storage. The ordinary constructor remains memory-only.
        static std::shared_ptr<const SourcePeaks>
        createPaged(storage::AudioShape,
                    std::vector<std::vector<gui::PeakLevel>>,
                    std::shared_ptr<storage::DecodedBlockCache> cache = {},
                    const std::function<bool()> &cancel = {});
        std::size_t levelSize(int channel, std::size_t level) const;
        void readPeaks(int channel, std::size_t level, std::size_t first,
                       std::span<Peak> output) const;
        struct Residency
        {
            uint64_t residentBytes, pagedBytes, bytesRead;
        };
        Residency residency() const;
        SourcePeaks(storage::AudioShape shape,
                    std::vector<std::vector<gui::PeakLevel>> levels)
            : dimensions(shape), channels(std::move(levels))
        {
            if (shape.frames < 0 || shape.channels <= 0 ||
                int(channels.size()) != shape.channels)
            {
                throw std::invalid_argument("Invalid source peak shape");
            }
            uint64_t bytes = 0;
            const auto add = [&](uint64_t count)
            {
                if (count > (UINT64_MAX - bytes) / sizeof(Peak))
                {
                    throw std::overflow_error("Peak allocation size overflow");
                }
                bytes += count * sizeof(Peak);
            };
            for (const auto &channel : channels)
            {
                for (const auto &level : channel)
                {
                    add(level.size());
                }
                auto count = channel.empty() ? 0 : channel.back().size();
                while (count > 1)
                {
                    count = count / 2 + count % 2;
                    add(count);
                }
            }
            residentMemory =
                storage::reserveWorking(bytes, storage::MemoryUse::Peaks);
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
                end < first || uint64_t(end) > levelSize(channel, 0))
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
                Peak peak;
                readPeaks(channel, level, uint64_t(first) >> level,
                          std::span<Peak>(&peak, 1));
                result = combine(result, peak);
                first += int64_t(1) << level;
                ++visited;
            }
            return result;
        }
    };
} // namespace cupuacu::waveform
