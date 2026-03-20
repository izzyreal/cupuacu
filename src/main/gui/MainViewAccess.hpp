#pragma once

#include "../State.hpp"
#include "ComponentLookup.hpp"
#include "MainView.hpp"

namespace cupuacu::gui
{
    inline cupuacu::gui::MainView *getMainViewIfPresent(cupuacu::State *state)
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

        return findComponentOfType<cupuacu::gui::MainView>(
            window->getRootComponent());
    }

    inline void requestMainViewRefresh(cupuacu::State *state)
    {
        auto *mainView = getMainViewIfPresent(state);
        if (!mainView)
        {
            return;
        }

        mainView->setDirty();
    }

    inline void rebuildMainWaveforms(cupuacu::State *state)
    {
        auto *mainView = getMainViewIfPresent(state);
        if (!mainView)
        {
            return;
        }

        mainView->rebuildWaveforms();
    }
} // namespace cupuacu::gui
