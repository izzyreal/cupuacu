#include "SourcePeaks.hpp"
#include "ProgressivePeaks.hpp"
#include "../storage/AudioBlockStore.hpp"
#include "../LongTask.hpp"
#include <chrono>

namespace cupuacu::waveform
{
    namespace
    {
        constexpr std::size_t tileDepth = 8;
        constexpr std::size_t tileWidth = std::size_t{1} << tileDepth;
        constexpr std::size_t tileScalars = tileWidth * 4;
        constexpr std::size_t tileBytes = tileScalars * sizeof(float);
    } // namespace

    struct SourcePeaks::PagedData
    {
        struct Level
        {
            uint64_t offset = 0, count = 0;
            unsigned local = 0;
        };
        std::vector<std::vector<Level>> levels;
        // Min/max pairs are scalar float blocks, using the same bounded cache
        // as decoded samples. Their index is arithmetic, not a resident page
        // table.
        std::shared_ptr<storage::AudioBlockStore> store;
        std::shared_ptr<storage::DecodedBlockCache> cache;
        uint64_t scalars = 0;
    };

    std::shared_ptr<const SourcePeaks>
    SourcePeaks::createPaged(storage::AudioShape shape,
                             std::vector<std::vector<gui::PeakLevel>> levels,
                             std::shared_ptr<storage::DecodedBlockCache> cache,
                             const std::function<bool()> &cancel)
    {
        auto result = std::make_shared<SourcePeaks>(shape, std::move(levels));
        auto check = [&]
        {
            if (cancel && cancel())
            {
                throw LongTaskCanceledError();
            }
        };
        check();
        if (result->channels.front().front().size() <= residentLevelLimit)
        {
            return result;
        }
        auto pages = std::make_shared<PagedData>();
        pages->cache =
            cache ? std::move(cache) : storage::defaultDecodedBlockCache();
        static std::atomic<uint64_t> sequence{0};
        const auto name =
            "cupuacu-peaks-" +
            std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()) +
            "-" + std::to_string(sequence.fetch_add(1));
        pages->store = std::make_shared<storage::AudioBlockStore>(
            std::filesystem::temp_directory_path() / name);
        // Small spatial subtrees keep boundary levels together without
        // fetching hundreds of KiB for a zoomed-out summary.
        constexpr std::size_t depth = tileDepth, width = tileWidth;
        std::array<float, tileScalars> buffer;
        for (auto &channel : result->channels)
        {
            auto &index = pages->levels.emplace_back(channel.size());
            for (std::size_t group = 0; group < channel.size(); group += depth)
            {
                const auto baseSize = channel[group].size();
                if (baseSize <= residentLevelLimit)
                {
                    break;
                }
                const auto tiles = (baseSize - 1) / width + 1;
                const auto firstBlock = pages->scalars / tileScalars;
                if (tiles >
                    (UINT64_MAX / sizeof(float) - pages->scalars) / tileScalars)
                {
                    throw std::overflow_error("Peak storage size overflow");
                }
                for (std::size_t tile = 0; tile < tiles; ++tile)
                {
                    check();
                    buffer.fill(0);
                    for (std::size_t l = group;
                         l < std::min(channel.size(), group + depth); ++l)
                    {
                        const auto &level = channel[l];
                        if (level.size() <= residentLevelLimit)
                        {
                            break;
                        }
                        const auto local = unsigned(l - group);
                        index[l] = {firstBlock, level.size(), local};
                        const auto localWidth = width >> local;
                        const auto offset = 2 * width - (2 * width >> local);
                        const auto first = tile * localWidth;
                        const auto count =
                            first < level.size()
                                ? std::min(localWidth, level.size() - first)
                                : 0;
                        for (std::size_t i = 0; i < count; ++i)
                        {
                            const auto peak = level[first + i];
                            buffer[2 * (offset + i)] = peak.min;
                            buffer[2 * (offset + i) + 1] = peak.max;
                        }
                    }
                    pages->store->append(buffer);
                    pages->scalars += buffer.size();
                }
                for (std::size_t l = group;
                     l < std::min(channel.size(), group + depth); ++l)
                {
                    if (index[l].count)
                    {
                        channel[l] = {};
                    }
                }
            }
        }
        pages->store->flush();
        result->paged = std::move(pages);
        auto remainingMemory = storage::reserveWorking(
            result->residency().residentBytes, storage::MemoryUse::Peaks);
        result->residentMemory = std::move(remainingMemory);
        return result;
    }

    std::shared_ptr<const SourcePeaks> SourcePeaks::createStreaming(
        storage::AudioShape shape, const ReadBasePeaks &read,
        std::shared_ptr<storage::DecodedBlockCache> cache,
        const std::function<bool()> &cancel)
    {
        if (shape.frames < 0 || shape.channels <= 0 || !read)
        {
            throw std::invalid_argument("Invalid streaming peak source");
        }
        auto check = [&]
        {
            if (cancel && cancel())
            {
                throw LongTaskCanceledError();
            }
        };
        check();
        std::vector<uint64_t> counts{uint64_t(
            shape.frames / blockFrames + (shape.frames % blockFrames != 0))};
        while (counts.back() > 1)
        {
            counts.push_back((counts.back() + 1) / 2);
        }
        auto result =
            std::shared_ptr<SourcePeaks>(new SourcePeaks(shape, counts.size()));
        if (counts.front() <= residentLevelLimit)
        {
            auto scratch = storage::reserveWorking(
                counts.front() * sizeof(Peak), storage::MemoryUse::Peaks);
            uint64_t bytes = 0;
            for (const auto count : counts)
            {
                bytes += count * sizeof(Peak) * shape.channels;
            }
            result->residentMemory =
                storage::reserveWorking(bytes, storage::MemoryUse::Peaks);
            for (int c = 0; c < shape.channels; ++c)
            {
                check();
                std::vector<Peak> base(counts[0]);
                read(c, 0, base);
                result->channels[c][0].resize(base.size());
                for (std::size_t i = 0; i < base.size(); ++i)
                {
                    result->channels[c][0].set(i, base[i]);
                }
                for (std::size_t l = 1; l < counts.size(); ++l)
                {
                    auto &level = result->channels[c][l];
                    const auto &previous = result->channels[c][l - 1];
                    level.resize(counts[l]);
                    for (std::size_t i = 0; i < level.size(); ++i)
                    {
                        level.set(i, i * 2 + 1 < previous.size()
                                         ? combine(previous[i * 2],
                                                   previous[i * 2 + 1])
                                         : previous[i * 2]);
                    }
                }
            }
            check();
            return result;
        }
        auto builder = std::make_shared<ProgressivePeaks>(
            shape,
            cache ? std::move(cache) : storage::defaultDecodedBlockCache());
        // Keep input reads large and sequential while sealing small output
        // tiles. Parent summaries propagate during append, without rereading
        // the detailed tiles to construct the next group.
        auto scratch = storage::reserveWorking(16384 * sizeof(Peak),
                                               storage::MemoryUse::Peaks);
        std::array<Peak, 16384> buffer;
        for (int c = 0; c < shape.channels; ++c)
        {
            for (uint64_t first = 0; first < counts[0];)
            {
                check();
                auto chunk = std::span(buffer).first(
                    std::min<uint64_t>(buffer.size(), counts[0] - first));
                read(c, first, chunk);
                builder->append(c, chunk);
                first += chunk.size();
            }
        }
        check();
        builder->publish(shape.frames);
        return builder->finish();
    }

    std::shared_ptr<const SourcePeaks> SourcePeaks::fromReader(
        storage::AudioShape shape,
        std::function<void(int, std::size_t, uint64_t, std::span<Peak>)> read,
        std::function<std::array<uint64_t, 3>()> stats)
    {
        if (shape.frames < 0 || shape.channels <= 0 || !read || !stats)
        {
            throw std::invalid_argument("Invalid external peak reader");
        }
        const uint64_t count = shape.frames / 128 + (shape.frames % 128 != 0);
        auto result = std::shared_ptr<SourcePeaks>(new SourcePeaks(
            shape, count > 1 ? std::bit_width(count - 1) + 1 : 1));
        result->externalRead = std::move(read);
        result->externalStats = std::move(stats);
        return result;
    }

    std::size_t SourcePeaks::levelSize(int channel, std::size_t level) const
    {
        const auto size = channels.at(channel).at(level).size();
        if (externalRead)
        {
            const uint64_t count =
                dimensions.frames / 128 + (dimensions.frames % 128 != 0);
            const uint64_t width = uint64_t{1} << level;
            return count / width + (count % width != 0);
        }
        return paged && paged->levels[channel][level].count
                   ? paged->levels[channel][level].count
                   : size;
    }

    void SourcePeaks::readPeaks(int channel, std::size_t level,
                                std::size_t first, std::span<Peak> output) const
    {
        const auto size = levelSize(channel, level);
        if (first > size || output.size() > size - first)
        {
            throw std::out_of_range("Read outside source peaks");
        }
        if (externalRead)
        {
            externalRead(channel, level, first, output);
            return;
        }
        if (!paged || !paged->levels[channel][level].count)
        {
            std::copy_n(channels[channel][level].begin() + first, output.size(),
                        output.begin());
            return;
        }
        const auto &location = paged->levels[channel][level];
        const auto width = tileWidth >> location.local;
        const auto levelOffset =
            2 * tileWidth - (2 * tileWidth >> location.local);
        std::array<float, 256> buffer;
        while (!output.empty())
        {
            constexpr uint64_t blocksPerSegment = 64 * 1024 * 1024 / tileBytes;
            const auto blockIndex = location.offset + first / width;
            storage::AudioBlock block{blockIndex / blocksPerSegment,
                                      blockIndex % blocksPerSegment * tileBytes,
                                      uint32_t(tileScalars)};
            const auto local = first % width;
            const auto offset = uint32_t((levelOffset + local) * 2);
            const auto count =
                std::min({output.size(), buffer.size() / 2, width - local});
            paged->cache->read(*paged->store, block, offset,
                               std::span<float>(buffer).first(count * 2));
            for (std::size_t i = 0; i < count; ++i)
            {
                output[i] = {buffer[i * 2], buffer[i * 2 + 1]};
            }
            output = output.subspan(count);
            first += count;
        }
    }

    SourcePeaks::Residency SourcePeaks::residency() const
    {
        if (externalStats)
        {
            const auto values = externalStats();
            return {values[0], values[1], values[2]};
        }
        uint64_t resident = 0;
        for (const auto &channel : channels)
        {
            for (const auto &level : channel)
            {
                resident += level.size() * sizeof(Peak);
            }
        }
        return {resident, paged ? paged->scalars * sizeof(float) : 0,
                paged ? paged->store->ioBytes().first : 0};
    }
} // namespace cupuacu::waveform
