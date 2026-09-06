#include "MemoryResources.hpp"
#include "concurrency/LatestValueWorker.hpp"
#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>
#ifdef __APPLE__
#include <dispatch/dispatch.h>
#endif

namespace cupuacu::storage
{
    std::shared_ptr<WorkingMemory> defaultWorkingMemory()
    {
        return defaultDecodedBlockCache();
    }
    std::shared_ptr<DecodedBlockCache> defaultDecodedBlockCache()
    {
        static const auto cache = std::make_shared<DecodedBlockCache>(
            DecodedBlockCache::defaultByteBudget(
                uint64_t(std::max(SDL_GetSystemRAM(), 0)) * 1024 * 1024),
            true);
        return cache;
    }

    uint64_t readAudioMemoryBudget(const std::filesystem::path &settings,
                                   uint64_t physicalBytes)
    {
        const auto automatic =
            DecodedBlockCache::defaultByteBudget(physicalBytes);
        if (!std::filesystem::exists(settings))
        {
            return automatic;
        }
        std::ifstream input(settings);
        const auto json = nlohmann::json::parse(input);
        if (!json.is_object())
        {
            throw std::runtime_error("Expected performance settings object");
        }
        if (!json.contains("audio_memory_mib"))
        {
            return automatic;
        }
        const auto &value = json.at("audio_memory_mib");
        if (!value.is_number_unsigned())
        {
            throw std::runtime_error(
                "audio_memory_mib must be a nonnegative integer");
        }
        const auto mib = value.get<uint64_t>();
        if (mib > UINT64_MAX / (1024 * 1024))
        {
            throw std::runtime_error("audio_memory_mib is too large");
        }
        return mib ? mib * 1024 * 1024 : automatic;
    }

    struct MemoryPressureMonitor::Impl
    {
        using Worker = concurrency::LatestValueWorker<unsigned, bool>;
        std::shared_ptr<Worker> worker;
#ifdef __APPLE__
        struct Context
        {
            dispatch_source_t source;
            std::shared_ptr<Worker> worker;
        };
        dispatch_source_t source = nullptr;
#endif
        explicit Impl(std::shared_ptr<DecodedBlockCache> cache)
            : worker(std::make_shared<Worker>(
                  [cache = std::move(cache)](
                      unsigned level, const auto &) -> std::optional<bool>
                  {
                      cache->setPressure(level);
                      return true;
                  }))
        {
#ifdef __APPLE__
            source = dispatch_source_create(
                DISPATCH_SOURCE_TYPE_MEMORYPRESSURE, 0,
                DISPATCH_MEMORYPRESSURE_NORMAL | DISPATCH_MEMORYPRESSURE_WARN |
                    DISPATCH_MEMORYPRESSURE_CRITICAL,
                dispatch_get_global_queue(QOS_CLASS_UTILITY, 0));
            if (source)
            {
                dispatch_set_context(source, new Context{source, worker});
                dispatch_source_set_event_handler_f(
                    source,
                    [](void *raw)
                    {
                        const auto &context = *static_cast<Context *>(raw);
                        const auto flags =
                            dispatch_source_get_data(context.source);
                        context.worker->submit(
                            flags & DISPATCH_MEMORYPRESSURE_CRITICAL ? 2
                            : flags & DISPATCH_MEMORYPRESSURE_WARN   ? 1
                                                                     : 0);
                    });
                dispatch_source_set_cancel_handler_f(
                    source,
                    [](void *raw)
                    {
                        auto *context = static_cast<Context *>(raw);
                        dispatch_release(context->source);
                        delete context;
                    });
                dispatch_resume(source);
            }
#endif
        }
        ~Impl()
        {
#ifdef __APPLE__
            // The cancellation handler releases the callback's worker after
            // all callbacks finish. Neither can access the destroyed monitor.
            if (source)
            {
                dispatch_source_cancel(source);
            }
#endif
        }
    };
    MemoryPressureMonitor::MemoryPressureMonitor(
        std::shared_ptr<DecodedBlockCache> cache)
        : impl(std::make_unique<Impl>(std::move(cache)))
    {
    }
    MemoryPressureMonitor::~MemoryPressureMonitor() = default;
    void MemoryPressureMonitor::notify(unsigned level)
    {
        impl->worker->submit(level);
    }
} // namespace cupuacu::storage
