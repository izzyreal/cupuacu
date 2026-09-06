#include "ProgressivePeaks.hpp"
#include "../storage/AudioBlockStore.hpp"
#include <chrono>
#include <shared_mutex>

namespace cupuacu::waveform
{
    struct ProgressivePeaks::Impl
    {
        static constexpr uint64_t tileWidth = 16384;
        struct Group
        {
            uint64_t expected, produced = 0, sealed = 0, width;
            bool resident;
            std::vector<float> active;
            std::shared_ptr<storage::AudioBlockStore> store;
            explicit Group(uint64_t count)
                : expected(count),
                  width(
                      std::bit_ceil(std::clamp(count, uint64_t{1}, tileWidth))),
                  resident(count <= SourcePeaks::residentLevelLimit),
                  active(width * 4)
            {
            }
            Peak get(uint64_t index) const
            {
                return {active[index * 2], active[index * 2 + 1]};
            }
            void put(uint64_t index, Peak peak)
            {
                active[index * 2] = peak.min;
                active[index * 2 + 1] = peak.max;
            }
            uint64_t offset(unsigned level) const
            {
                return 2 * width - (2 * width >> level);
            }
            void seal()
            {
                if (!store)
                {
                    static std::atomic<uint64_t> sequence{0};
                    store = std::make_shared<storage::AudioBlockStore>(
                        std::filesystem::temp_directory_path() /
                        ("cupuacu-import-peaks-" +
                         std::to_string(std::chrono::steady_clock::now()
                                            .time_since_epoch()
                                            .count()) +
                         "-" + std::to_string(sequence.fetch_add(1))));
                }
                store->append(active);
                ++sealed;
            }
        };
        storage::AudioShape shape;
        std::shared_ptr<storage::DecodedBlockCache> cache;
        mutable std::shared_mutex mutex;
        std::vector<std::vector<Group>> channels;
        std::atomic<int64_t> available{0};
        bool finished = false, failed = false;
        Impl(storage::AudioShape shape,
             std::shared_ptr<storage::DecodedBlockCache> cache)
            : shape(shape), cache(std::move(cache))
        {
            if (shape.frames < 0 || shape.channels <= 0 || !this->cache)
            {
                throw std::invalid_argument(
                    "Invalid progressive peak shape/cache");
            }
            channels.resize(shape.channels);
            for (auto &channel : channels)
            {
                uint64_t count = shape.frames / 128 + (shape.frames % 128 != 0);
                for (;;)
                {
                    channel.emplace_back(count);
                    if (count <= tileWidth)
                    {
                        break;
                    }
                    count = count / tileWidth + (count % tileWidth != 0);
                }
            }
        }
        void append(int c, std::size_t group, Peak peak)
        {
            auto &g = channels[c][group];
            if (g.produced >= g.expected)
            {
                throw std::logic_error("Too many progressive peaks");
            }
            auto index = g.produced % g.width;
            g.put(index, peak);
            unsigned level = 0;
            while (index & 1)
            {
                peak = combine(g.get(g.offset(level) + index - 1), peak);
                index >>= 1;
                g.put(g.offset(++level) + index, peak);
            }
            ++g.produced;
            if (g.produced % g.width == 0 && !g.resident)
            {
                peak = g.get(2 * g.width - 2);
                g.seal();
                if (group + 1 < channels[c].size())
                {
                    append(c, group + 1, peak);
                }
            }
        }
        void read(int c, std::size_t level, uint64_t first,
                  std::span<Peak> output) const
        {
            const auto &channel = channels.at(c);
            const auto group = std::min(level / 14, channel.size() - 1);
            const auto &g = channel[group];
            const auto local = unsigned(level - group * 14);
            const auto width = g.width >> local;
            if (!width)
            {
                throw std::out_of_range("Invalid progressive peak level");
            }
            std::array<float, 256> scratch;
            while (!output.empty())
            {
                const auto tile = first / width, at = first % width;
                const auto count =
                    std::min({output.size(), std::size_t(width - at),
                              scratch.size() / 2});
                const auto offset = g.offset(local) + at;
                if (g.resident || tile == g.sealed)
                {
                    for (std::size_t i = 0; i < count; ++i)
                    {
                        output[i] = g.get(offset + i);
                    }
                }
                else
                {
                    const auto scalars = g.width * 4,
                               bytes = scalars * sizeof(float);
                    const auto perSegment = 64 * 1024 * 1024 / bytes;
                    storage::AudioBlock block{tile / perSegment,
                                              tile % perSegment * bytes,
                                              uint32_t(scalars)};
                    cache->read(*g.store, block, uint32_t(offset * 2),
                                std::span(scratch).first(count * 2));
                    for (std::size_t i = 0; i < count; ++i)
                    {
                        output[i] = {scratch[i * 2], scratch[i * 2 + 1]};
                    }
                }
                first += count;
                output = output.subspan(count);
            }
        }
    };
    ProgressivePeaks::ProgressivePeaks(
        storage::AudioShape shape,
        std::shared_ptr<storage::DecodedBlockCache> cache)
        : impl(std::make_unique<Impl>(shape, std::move(cache)))
    {
    }
    ProgressivePeaks::~ProgressivePeaks() = default;
    void ProgressivePeaks::append(int c, std::span<const Peak> peaks)
    {
        auto &s = *impl;
        std::unique_lock lock(s.mutex);
        if (s.finished || s.failed || c < 0 || c >= s.shape.channels)
        {
            throw std::logic_error("Invalid progressive peak append");
        }
        s.failed = true;
        for (auto peak : peaks)
        {
            s.append(c, 0, peak);
        }
        s.failed = false;
    }
    void ProgressivePeaks::publish(int64_t frames)
    {
        auto &s = *impl;
        std::unique_lock lock(s.mutex);
        if (s.finished || s.failed || frames < s.available ||
            frames > s.shape.frames)
        {
            throw std::logic_error("Invalid progressive peak publication");
        }
        const auto count =
            frames / 128 + (frames == s.shape.frames && frames % 128 != 0);
        for (const auto &c : s.channels)
        {
            if (c[0].produced != uint64_t(count))
            {
                throw std::logic_error("Incomplete peak channels");
            }
        }
        s.available.store(frames == s.shape.frames ? frames
                                                   : frames / 128 * 128,
                          std::memory_order_release);
    }
    int64_t ProgressivePeaks::availableFrames() const
    {
        return impl->available.load(std::memory_order_acquire);
    }
    Peak ProgressivePeaks::queryBlocks(int c, int64_t first, int64_t end) const
    {
        auto &s = *impl;
        const auto frames = availableFrames();
        if (c < 0 || c >= s.shape.channels || first < 0 || end < first ||
            end > frames / 128 + (frames % 128 != 0))
        {
            throw std::out_of_range("Unpublished peak query");
        }
        std::shared_lock lock(s.mutex);
        auto result = emptyPeak();
        while (first < end)
        {
            const auto level =
                std::min(int(std::bit_width(uint64_t(end - first))) - 1,
                         first ? int(std::countr_zero(uint64_t(first))) : 63);
            Peak peak;
            s.read(c, level, uint64_t(first) >> level, std::span(&peak, 1));
            result = combine(result, peak);
            first += int64_t{1} << level;
        }
        return result;
    }
    std::shared_ptr<const SourcePeaks> ProgressivePeaks::finish()
    {
        auto &s = *impl;
        std::unique_lock lock(s.mutex);
        if (s.finished || s.failed || availableFrames() != s.shape.frames)
        {
            throw std::logic_error("Incomplete progressive peaks");
        }
        s.failed = true;
        for (int c = 0; c < s.shape.channels; ++c)
        {
            for (std::size_t group = 0; group < s.channels[c].size(); ++group)
            {
                auto &g = s.channels[c][group];
                if (g.produced != g.expected)
                {
                    throw std::logic_error("Incomplete peak group");
                }
                const auto count = g.produced % g.width;
                if (count)
                {
                    auto index = count - 1;
                    auto peak = g.get(index);
                    for (unsigned level = 0; (g.width >> level) > 1;)
                    {
                        if (index & 1)
                        {
                            peak = combine(g.get(g.offset(level) + index - 1),
                                           peak);
                        }
                        index >>= 1;
                        g.put(g.offset(++level) + index, peak);
                    }
                    if (!g.resident)
                    {
                        g.seal();
                    }
                    if (group + 1 < s.channels[c].size())
                    {
                        s.append(c, group + 1, peak);
                    }
                }
                if (!g.resident)
                {
                    g.store->flush();
                    std::vector<float>().swap(g.active);
                }
            }
        }
        s.finished = true;
        s.failed = false;
        auto self = shared_from_this();
        return SourcePeaks::fromReader(
            s.shape,
            [self](int c, std::size_t level, uint64_t first,
                   std::span<Peak> out)
            {
                std::shared_lock lock(self->impl->mutex);
                self->impl->read(c, level, first, out);
            },
            [self]
            {
                return self->residency();
            });
    }
    std::array<uint64_t, 3> ProgressivePeaks::residency() const
    {
        std::shared_lock lock(impl->mutex);
        std::array<uint64_t, 3> result{};
        for (const auto &c : impl->channels)
        {
            for (const auto &g : c)
            {
                result[0] += g.active.capacity() * sizeof(float);
                if (g.store)
                {
                    const auto [read, written] = g.store->ioBytes();
                    result[1] += written;
                    result[2] += read;
                }
            }
        }
        return result;
    }
} // namespace cupuacu::waveform
