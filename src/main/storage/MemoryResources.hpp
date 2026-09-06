#pragma once
#include "AudioBlockStore.hpp"
#include <filesystem>

namespace cupuacu::storage
{
    // Zero means automatic (10% of physical RAM). Invalid settings throw;
    // startup reports the error and retains the automatic default.
    uint64_t readAudioMemoryBudget(const std::filesystem::path &settings,
                                   uint64_t physicalBytes);

    class MemoryPressureMonitor
    {
        struct Impl;
        std::unique_ptr<Impl> impl;

    public:
        explicit MemoryPressureMonitor(
            std::shared_ptr<DecodedBlockCache> cache);
        ~MemoryPressureMonitor();
        // Posts a coalesced request; reclamation never runs on the caller.
        void notify(unsigned level);
    };
} // namespace cupuacu::storage
