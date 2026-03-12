#include "TestSdlTtfGuard.hpp"

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <mutex>
#include <stdexcept>
#include <string>
#include <cstdlib>

namespace
{
    std::mutex gGuardMutex;
    int gGuardRefCount = 0;
    bool gInitializedVideoHere = false;
} // namespace

namespace cupuacu::test
{
    SdlTtfGuard::SdlTtfGuard()
    {
        std::lock_guard<std::mutex> lock(gGuardMutex);
        if (gGuardRefCount == 0)
        {
            setenv("SDL_VIDEODRIVER", "offscreen", 0);
            if ((SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO) == 0)
            {
                if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
                {
                    throw std::runtime_error(
                        std::string("SDL_InitSubSystem(SDL_INIT_VIDEO) failed: ") +
                        SDL_GetError());
                }
                gInitializedVideoHere = true;
            }

            if (!TTF_Init())
            {
                if (gInitializedVideoHere)
                {
                    SDL_QuitSubSystem(SDL_INIT_VIDEO);
                    gInitializedVideoHere = false;
                }
                throw std::runtime_error(
                    std::string("TTF_Init failed: ") + SDL_GetError());
            }
        }
        ++gGuardRefCount;
    }

    SdlTtfGuard::~SdlTtfGuard()
    {
        std::lock_guard<std::mutex> lock(gGuardMutex);
        --gGuardRefCount;
        if (gGuardRefCount == 0)
        {
            TTF_Quit();
            if (gInitializedVideoHere)
            {
                SDL_QuitSubSystem(SDL_INIT_VIDEO);
                gInitializedVideoHere = false;
            }
        }
    }

    void ensureSdlTtfInitialized()
    {
        static SdlTtfGuard guard{};
        (void)guard;
    }
} // namespace cupuacu::test
