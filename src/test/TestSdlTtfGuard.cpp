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
    SDL_LogOutputFunction gPreviousLogOutputFunction = nullptr;
    void *gPreviousLogOutputUserData = nullptr;

    void filteredTestLogOutput(void *userdata, const int category,
                               const SDL_LogPriority priority,
                               const char *message)
    {
        const std::string_view text = message == nullptr ? "" : message;
        if (text.find("SDL_CreateWindowAndRenderer() failed: The video driver did not add any displays") !=
            std::string_view::npos)
        {
            return;
        }

        if (gPreviousLogOutputFunction != nullptr)
        {
            gPreviousLogOutputFunction(gPreviousLogOutputUserData, category,
                                       priority, message);
        }
    }
} // namespace

namespace cupuacu::test
{
    SdlTtfGuard::SdlTtfGuard()
    {
        std::lock_guard<std::mutex> lock(gGuardMutex);
        if (gGuardRefCount == 0)
        {
            SDL_GetLogOutputFunction(&gPreviousLogOutputFunction,
                                     &gPreviousLogOutputUserData);
            SDL_SetLogOutputFunction(filteredTestLogOutput, nullptr);
            if (!TTF_Init())
            {
                SDL_SetLogOutputFunction(gPreviousLogOutputFunction,
                                         gPreviousLogOutputUserData);
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
            SDL_SetLogOutputFunction(gPreviousLogOutputFunction,
                                     gPreviousLogOutputUserData);
            gPreviousLogOutputFunction = nullptr;
            gPreviousLogOutputUserData = nullptr;
        }
    }

    void ensureSdlTtfInitialized()
    {
        static SdlTtfGuard guard{};
        (void)guard;
    }
} // namespace cupuacu::test
