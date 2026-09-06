#pragma once
#include "actions/audio/RevisionEdit.hpp"
#include <chrono>
#include <thread>
namespace cupuacu::test
{
    inline void finishRevisionCommands(State *state)
    {
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (!state->revisionCommands.empty())
        {
            actions::audio::processPendingRevisionCommands(state);
            if (std::chrono::steady_clock::now() > deadline)
            {
                throw std::runtime_error("Revision command timed out");
            }
            if (!state->revisionCommands.empty())
            {
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        }
    }
} // namespace cupuacu::test
