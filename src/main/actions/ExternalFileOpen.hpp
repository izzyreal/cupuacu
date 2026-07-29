#pragma once

#include "io/BackgroundOpen.hpp"

#include <SDL3/SDL_events.h>

#include <string>
#include <vector>

namespace cupuacu::actions
{
    inline std::vector<std::string>
    collectExternalFileArguments(const int argc, char *const *argv)
    {
        std::vector<std::string> paths;
        if (argc <= 1 || argv == nullptr)
        {
            return paths;
        }

        paths.reserve(static_cast<std::size_t>(argc - 1));
        for (int index = 1; index < argc; ++index)
        {
            if (argv[index] != nullptr && argv[index][0] != '\0')
            {
                paths.emplace_back(argv[index]);
            }
        }
        return paths;
    }

    inline void
    queueExternalFileArguments(cupuacu::State *state,
                               const std::vector<std::string> &paths)
    {
        for (const auto &path : paths)
        {
            cupuacu::actions::io::queueOpenFile(state, path);
        }
    }

    inline bool queueExternalFileEvent(cupuacu::State *state,
                                       const SDL_Event *event)
    {
        if (event == nullptr || event->type != SDL_EVENT_DROP_FILE)
        {
            return false;
        }

        if (event->drop.data != nullptr && event->drop.data[0] != '\0')
        {
            cupuacu::actions::io::queueOpenFile(state, event->drop.data);
        }
        return true;
    }
} // namespace cupuacu::actions
