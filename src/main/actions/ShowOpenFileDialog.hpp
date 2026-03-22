#pragma once
#include <SDL3/SDL.h>

#include "../file/AudioExport.hpp"
#include "DocumentLifecycle.hpp"

#include "../State.hpp"

#include <memory>
#include <string>
#include <vector>

namespace cupuacu::actions
{
    static SDL_Window *getFileDialogParentWindow(cupuacu::State *state)
    {
        if (!state)
        {
            return nullptr;
        }
        if (state->modalWindow && state->modalWindow->isOpen())
        {
            return state->modalWindow->getSdlWindow();
        }
        if (state->mainDocumentSessionWindow &&
            state->mainDocumentSessionWindow->getWindow() &&
            state->mainDocumentSessionWindow->getWindow()->isOpen())
        {
            return state->mainDocumentSessionWindow->getWindow()->getSdlWindow();
        }
        return nullptr;
    }

    struct OpenDialogFilterSet
    {
        std::vector<std::string> names;
        std::vector<std::string> patterns;
        std::vector<SDL_DialogFileFilter> filters;
    };

    static OpenDialogFilterSet buildOpenDialogFilters()
    {
        OpenDialogFilterSet result;
        const auto openFormats = file::probeAvailableOpenFormats();
        if (openFormats.empty())
        {
            return result;
        }

        std::string combinedPattern;
        for (const auto &format : openFormats)
        {
            result.names.push_back(format.label);

            std::string pattern;
            for (std::size_t i = 0; i < format.extensions.size(); ++i)
            {
                if (i > 0)
                {
                    pattern += ";";
                }
                pattern += format.extensions[i];

                if (!combinedPattern.empty())
                {
                    combinedPattern += ";";
                }
                combinedPattern += format.extensions[i];
            }
            result.patterns.push_back(std::move(pattern));
        }

        result.names.insert(result.names.begin(), "All supported audio");
        result.patterns.insert(result.patterns.begin(), std::move(combinedPattern));
        result.filters.reserve(result.names.size());
        for (std::size_t i = 0; i < result.names.size(); ++i)
        {
            result.filters.push_back(
                SDL_DialogFileFilter{result.names[i].c_str(),
                                     result.patterns[i].c_str()});
        }

        return result;
    }

    static const OpenDialogFilterSet &getOpenDialogFilters()
    {
        static const OpenDialogFilterSet filters = buildOpenDialogFilters();
        return filters;
    }

    static void fileDialogCallback(void *userdata, const char *const *filelist,
                                   int filter)
    {
        if (!filelist)
        {
            SDL_Log("An error occured: %s", SDL_GetError());
            return;
        }
        else if (!*filelist)
        {
            SDL_Log("The user did not select any file.");
            SDL_Log("Most likely, the dialog was canceled.");
            return;
        }

        auto *state = (cupuacu::State *)userdata;
        while (*filelist)
        {
            actions::loadFileIntoNewTab(state, *filelist);
            ++filelist;
        }
    }

    static void showOpenFileDialog(cupuacu::State *state)
    {
        const auto &filters = getOpenDialogFilters();
        const auto *filterPtr =
            filters.filters.empty() ? nullptr : filters.filters.data();
        const int filterCount = static_cast<int>(filters.filters.size());
        SDL_ShowOpenFileDialog(fileDialogCallback, state,
                               getFileDialogParentWindow(state), filterPtr,
                               filterCount, nullptr, true);
    }
} // namespace cupuacu::actions
