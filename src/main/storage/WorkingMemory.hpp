#pragma once

#include <cstdint>
#include <memory>
#include <stdexcept>

namespace cupuacu::storage
{
    enum class MemoryUse : unsigned
    {
        Index,
        Peaks,
        Import,
        Viewport,
        Transport,
        Conversion,
        Export,
        Container,
        Effect,
        Count
    };

    // Independent of the sample-store header so working indexes can use the
    // same admission policy without a dependency cycle.
    class WorkingMemory
    {
    public:
        virtual ~WorkingMemory() = default;
        virtual std::shared_ptr<void> tryReserveWorking(uint64_t,
                                                        MemoryUse) = 0;
    };

    std::shared_ptr<WorkingMemory> defaultWorkingMemory();

    inline std::shared_ptr<void>
    reserveWorking(uint64_t bytes, MemoryUse use,
                   std::shared_ptr<WorkingMemory> memory = {})
    {
        if (!bytes)
        {
            return {};
        }
        auto token = (memory ? memory : defaultWorkingMemory())
                         ->tryReserveWorking(bytes, use);
        if (!token)
        {
            throw std::runtime_error("Insufficient audio working memory");
        }
        return token;
    }
} // namespace cupuacu::storage
