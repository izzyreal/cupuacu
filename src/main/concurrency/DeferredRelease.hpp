#pragma once
#include <memory>

namespace cupuacu::concurrency
{
    // Final release is queued without allocating or joining on the caller.
    // Use at application ownership boundaries for disk-backed revisions.
    std::shared_ptr<const void>
    retainForBackgroundRelease(std::shared_ptr<const void> value);
    template <typename T>
    std::shared_ptr<T> releaseOnWorker(std::shared_ptr<T> value)
    {
        auto *pointer = value.get();
        return std::shared_ptr<T>(retainForBackgroundRelease(std::move(value)),
                                  pointer);
    }
} // namespace cupuacu::concurrency
