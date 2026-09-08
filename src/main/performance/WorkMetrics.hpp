#pragma once

// Only the diagnostic benchmark core enables these observations. Timing and
// application builds compile out both the counters and their argument
// evaluation.
#ifndef CUPUACU_WORK_METRICS
#define CUPUACU_WORK_METRICS 0
#endif

#include <array>
#include <atomic>
#include <cstdint>
#include <utility>
#include <istream>
#include <ostream>

#if CUPUACU_WORK_METRICS
#define CUPUACU_METRIC(...) __VA_ARGS__
#else
#define CUPUACU_METRIC(...) ((void)0)
#endif

namespace cupuacu::performance
{
    enum class Work : unsigned
    {
        SampleBytesCopied,
        MetadataBytesCopied,
        SamplesScanned,
        FullBufferClones,
        BasePeaksRebuilt,
        PeakBytesCopied,
        UndoBytesRead,
        UndoBytesWritten,
        DecodedBytesRead,
        PeakFileBytesRead,
        PeakFileBytesWritten,
        AutosaveBytesRead,
        AutosaveBytesWritten,
        Count
    };
    inline constexpr std::array names{
        "sample_bytes_copied",     "metadata_bytes_copied",
        "samples_scanned",         "full_buffer_clones",
        "base_peaks_rebuilt",      "peak_bytes_copied",
        "undo_bytes_read",         "undo_bytes_written",
        "decoded_bytes_read",      "peak_file_bytes_read",
        "peak_file_bytes_written", "autosave_bytes_read",
        "autosave_bytes_written"};

    struct Registry
    {
        std::array<std::atomic<std::uint64_t>, unsigned(Work::Count)> work{};
        std::atomic<std::uint64_t> liveBytes{0};
        std::atomic<std::uint64_t> peakBytes{0};
    };
    inline Registry registry;

    inline void add(Work kind, std::uint64_t value)
    {
        registry.work[unsigned(kind)].fetch_add(value,
                                                std::memory_order_relaxed);
    }
    inline void resetWork()
    {
        for (auto &counter : registry.work)
        {
            counter.store(0, std::memory_order_relaxed);
        }
        registry.peakBytes.store(registry.liveBytes.load());
    }

// Counts observed inner-vector capacities, not allocator overhead or the
// temporary overlap of old/new allocations inside std::vector growth.
#if CUPUACU_WORK_METRICS
    class Capacity
    {
    public:
        Capacity() = default;
        explicit Capacity(std::uint64_t value)
        {
            set(value);
        }
        Capacity(const Capacity &other)
        {
            set(other.bytes);
        }
        Capacity(Capacity &&other) noexcept
            : bytes(std::exchange(other.bytes, 0))
        {
        }
        Capacity &operator=(const Capacity &other)
        {
            set(other.bytes);
            return *this;
        }
        Capacity &operator=(Capacity &&other) noexcept
        {
            if (this != &other)
            {
                set(0);
                bytes = std::exchange(other.bytes, 0);
            }
            return *this;
        }
        ~Capacity()
        {
            set(0);
        }
        void set(std::uint64_t value)
        {
            if (value > bytes)
            {
                const auto live =
                    registry.liveBytes.fetch_add(value - bytes) + value - bytes;
                auto peak = registry.peakBytes.load();
                while (peak < live &&
                       !registry.peakBytes.compare_exchange_weak(peak, live))
                {
                }
            }
            else
            {
                registry.liveBytes.fetch_sub(bytes - value);
            }
            bytes = value;
        }

    private:
        std::uint64_t bytes = 0;
    };

#else
    struct Capacity
    {
        Capacity() = default;
        explicit Capacity(std::uint64_t) {}
        void set(std::uint64_t) {}
    };
#endif

    template <typename Matrix>
    std::uint64_t matrixCapacity(const Matrix &matrix)
    {
        std::uint64_t bytes = 0;
        for (const auto &row : matrix)
        {
            bytes += row.capacity() *
                     sizeof(typename Matrix::value_type::value_type);
        }
        return bytes;
    }
    template <typename Matrix> std::uint64_t matrixBytes(const Matrix &matrix)
    {
        std::uint64_t bytes = 0;
        for (const auto &row : matrix)
        {
            bytes +=
                row.size() * sizeof(typename Matrix::value_type::value_type);
        }
        return bytes;
    }
    template <typename Fn> struct OnExit
    {
        Fn fn;
        ~OnExit()
        {
            fn();
        }
    };
    inline auto observeRead(std::istream &stream, Work kind)
    {
        return OnExit{[&stream, kind]
                      {
                          if (stream.good())
                          {
                              const auto bytes = stream.tellg();
                              if (bytes >= 0)
                              {
                                  add(kind, static_cast<std::uint64_t>(bytes));
                              }
                          }
                      }};
    }
    inline auto observeWrite(std::ostream &stream, Work kind)
    {
        return OnExit{[&stream, kind]
                      {
                          if (stream.good())
                          {
                              const auto bytes = stream.tellp();
                              if (bytes >= 0)
                              {
                                  add(kind, static_cast<std::uint64_t>(bytes));
                              }
                          }
                      }};
    }
} // namespace cupuacu::performance
