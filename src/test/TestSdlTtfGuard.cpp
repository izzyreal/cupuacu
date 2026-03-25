#include "TestSdlTtfGuard.hpp"

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <mutex>
#include <stdexcept>
#include <string>

namespace
{
    std::mutex gGuardMutex;
    int gGuardRefCount = 0;
} // namespace

namespace cupuacu::test
{
    SdlTtfGuard::SdlTtfGuard()
    {
        std::lock_guard<std::mutex> lock(gGuardMutex);
        if (gGuardRefCount == 0)
        {
            if (!TTF_Init())
            {
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
        }
    }

    void ensureSdlTtfInitialized()
    {
        static SdlTtfGuard guard{};
        (void)guard;
    }
} // namespace cupuacu::test
