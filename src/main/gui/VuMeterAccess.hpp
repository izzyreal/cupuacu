#pragma once

#include "../State.hpp"
#include "ComponentLookup.hpp"
#include "VuMeter.hpp"

namespace cupuacu::gui
{
    inline cupuacu::gui::VuMeter *getVuMeterIfPresent(cupuacu::State *state)
    {
        if (!state || !state->mainDocumentSessionWindow)
        {
            return nullptr;
        }

        auto *window = state->mainDocumentSessionWindow->getWindow();
        if (!window)
        {
            return nullptr;
        }

        return findComponentOfType<cupuacu::gui::VuMeter>(
            window->getRootComponent());
    }

    inline void startVuMeterDecay(cupuacu::State *state)
    {
        auto *vuMeter = getVuMeterIfPresent(state);
        if (!vuMeter)
        {
            return;
        }

        vuMeter->startDecay();
    }
} // namespace cupuacu::gui
