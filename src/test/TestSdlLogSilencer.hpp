#pragma once

#include <SDL3/SDL.h>

namespace cupuacu::test
{
    class ScopedSdlLogSilencer
    {
    public:
        ScopedSdlLogSilencer()
        {
            SDL_GetLogOutputFunction(&previousCallback, &previousUserdata);
            SDL_SetLogOutputFunction(&ScopedSdlLogSilencer::discardLog, nullptr);
        }

        ~ScopedSdlLogSilencer()
        {
            SDL_SetLogOutputFunction(previousCallback, previousUserdata);
        }

        ScopedSdlLogSilencer(const ScopedSdlLogSilencer &) = delete;
        ScopedSdlLogSilencer &
        operator=(const ScopedSdlLogSilencer &) = delete;

    private:
        static void SDLCALL discardLog(void *, int, SDL_LogPriority,
                                       const char *)
        {
        }

        SDL_LogOutputFunction previousCallback = nullptr;
        void *previousUserdata = nullptr;
    };
} // namespace cupuacu::test
