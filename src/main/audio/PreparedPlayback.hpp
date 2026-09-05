#pragma once
#include "AudioBuffer.hpp"
#include "AudioProcessor.hpp"
#include "../playback/ReadAhead.hpp"
#include <atomic>

namespace cupuacu::audio
{
    // Owned on the control side until callback retirement. Messages and the
    // callback carry only a borrowed pointer; final reclamation runs
    // off-thread.
    struct PreparedPlayback
    {
        std::shared_ptr<const AudioBuffer> resident;
        std::unique_ptr<playback::ReadAhead> readAhead;
        std::shared_ptr<const AudioProcessor> processor;
        storage::AudioShape shape;
        std::atomic<bool> retired{false};
        void retire() noexcept
        {
            if (readAhead)
            {
                readAhead->close();
            }
            retired.store(true, std::memory_order_release);
        }
    };
} // namespace cupuacu::audio
