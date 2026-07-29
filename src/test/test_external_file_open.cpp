#include <catch2/catch_test_macros.hpp>

#include "TestPaths.hpp"
#include "actions/ExternalFileOpen.hpp"

#include <SDL3/SDL_events.h>

#include <string>
#include <vector>

TEST_CASE("External file arguments preserve paths and ordering", "[actions]")
{
    char executable[] = "Cupuacu";
    char firstPath[] = "/music/first recording.wav";
    char secondPath[] = "relative.flac";
    char *arguments[] = {executable, firstPath, secondPath};

    const auto paths =
        cupuacu::actions::collectExternalFileArguments(3, arguments);

    REQUIRE(paths == std::vector<std::string>{firstPath, secondPath});
}

TEST_CASE("External file arguments ignore missing and empty values",
          "[actions]")
{
    char executable[] = "Cupuacu";
    char empty[] = "";
    char filePath[] = "recording.mp3";
    char *arguments[] = {executable, nullptr, empty, filePath};

    REQUIRE(cupuacu::actions::collectExternalFileArguments(0, nullptr).empty());
    REQUIRE(
        cupuacu::actions::collectExternalFileArguments(1, arguments).empty());
    REQUIRE(cupuacu::actions::collectExternalFileArguments(4, arguments) ==
            std::vector<std::string>{filePath});
}

TEST_CASE("External file arguments use the normal asynchronous open queue",
          "[actions]")
{
    cupuacu::test::StateWithTestPaths state{};
    const std::vector<std::string> paths = {
        "/music/one.wav",
        "/music/two.aiff",
    };

    cupuacu::actions::queueExternalFileArguments(&state, paths);

    REQUIRE(state.pendingOpenFiles.size() == 2);
    REQUIRE(state.pendingOpenFiles[0].kind ==
            cupuacu::PendingOpenKind::UserOpen);
    REQUIRE(state.pendingOpenFiles[0].path == paths[0]);
    REQUIRE(state.pendingOpenFiles[0].updateRecentFiles);
    REQUIRE(state.pendingOpenFiles[1].path == paths[1]);
}

TEST_CASE("System file-open events use the normal asynchronous open queue",
          "[actions]")
{
    cupuacu::test::StateWithTestPaths state{};
    const std::string filePath = "/music/finder selection.ogg";
    SDL_Event event{};
    event.type = SDL_EVENT_DROP_FILE;
    event.drop.data = filePath.c_str();

    REQUIRE(cupuacu::actions::queueExternalFileEvent(&state, &event));
    REQUIRE(state.pendingOpenFiles.size() == 1);
    REQUIRE(state.pendingOpenFiles.front().kind ==
            cupuacu::PendingOpenKind::UserOpen);
    REQUIRE(state.pendingOpenFiles.front().path == filePath);

    SDL_Event unrelatedEvent{};
    unrelatedEvent.type = SDL_EVENT_DROP_TEXT;
    unrelatedEvent.drop.data = filePath.c_str();
    REQUIRE_FALSE(
        cupuacu::actions::queueExternalFileEvent(&state, &unrelatedEvent));
    REQUIRE(state.pendingOpenFiles.size() == 1);
}
