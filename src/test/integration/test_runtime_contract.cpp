#include <catch2/catch_test_macros.hpp>

#include <SDL3/SDL.h>

#include <string>

TEST_CASE("Integration runtime exposes the expected SDL drivers", "[integration]")
{
    const char *videoDriver = SDL_GetCurrentVideoDriver();
    if (videoDriver == nullptr)
    {
        videoDriver = SDL_getenv("SDL_VIDEODRIVER");
    }

    const char *audioDriver = SDL_getenv("SDL_AUDIODRIVER");
    const char *renderDriver = SDL_getenv("SDL_RENDER_DRIVER");

    REQUIRE(videoDriver != nullptr);
    REQUIRE(std::string(videoDriver) == "x11");
    REQUIRE(audioDriver != nullptr);
    REQUIRE(std::string(audioDriver) == "dummy");
    REQUIRE(renderDriver != nullptr);
    REQUIRE(std::string(renderDriver) == "software");
}
