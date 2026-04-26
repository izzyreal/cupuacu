#pragma once

#include <SDL3/SDL.h>

#include <string>

namespace cupuacu::actions
{
#ifdef __APPLE__
    bool showPlatformSaveFileDialog(SDL_DialogFileCallback callback,
                                    void *userdata,
                                    SDL_Window *parentWindow,
                                    const std::string &defaultLocation);
#else
    inline bool showPlatformSaveFileDialog(SDL_DialogFileCallback,
                                           void *,
                                           SDL_Window *,
                                           const std::string &)
    {
        return false;
    }
#endif
} // namespace cupuacu::actions
