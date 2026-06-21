#pragma once

#include <catch2/catch_test_macros.hpp>

#include "State.hpp"
#include "actions/effects/BackgroundEffect.hpp"

#include <chrono>

namespace cupuacu::test
{
    inline void drainPendingEffectWork(
        cupuacu::State *state,
        const std::chrono::milliseconds timeout = std::chrono::seconds(5))
    {
        if (!state || !state->backgroundEffectJob)
        {
            return;
        }

        if (!state->backgroundEffectJob->waitForCompletion(timeout))
        {
            const auto snapshot = state->backgroundEffectJob->snapshot();
            INFO("Background effect timed out after "
                 << timeout.count() << "ms: " << snapshot.detail);
            if (!snapshot.error.empty())
            {
                INFO("Background effect error: " << snapshot.error);
            }
            FAIL("Timed out waiting for background effect work");
        }

        cupuacu::actions::effects::processPendingEffectWork(state);
        if (state->backgroundEffectJob)
        {
            const auto snapshot = state->backgroundEffectJob->snapshot();
            INFO("Background effect remained queued after completion: "
                 << snapshot.detail);
            if (!snapshot.error.empty())
            {
                INFO("Background effect error: " << snapshot.error);
            }
            FAIL("Background effect work completed but did not commit");
        }
    }
} // namespace cupuacu::test
