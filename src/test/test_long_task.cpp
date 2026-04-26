#include <catch2/catch_test_macros.hpp>

#include "LongTask.hpp"
#include "TestPaths.hpp"
#include "gui/LongTaskOverlay.hpp"

TEST_CASE("Long task scope publishes and restores task state", "[long-task]")
{
    cupuacu::test::StateWithTestPaths state{};

    {
        cupuacu::LongTaskScope outer(&state, "Opening file", "large.wav",
                                     std::nullopt, false);
        REQUIRE(state.longTask.active);
        REQUIRE(state.longTask.title == "Opening file");
        REQUIRE(state.longTask.detail == "large.wav");

        {
            cupuacu::LongTaskScope inner(&state, "Autosaving document",
                                         "Preserving unsaved changes",
                                         std::nullopt, false);
            REQUIRE(state.longTask.active);
            REQUIRE(state.longTask.title == "Autosaving document");
            REQUIRE(state.longTask.detail == "Preserving unsaved changes");
        }

        REQUIRE(state.longTask.active);
        REQUIRE(state.longTask.title == "Opening file");
        REQUIRE(state.longTask.detail == "large.wav");
    }

    REQUIRE_FALSE(state.longTask.active);
    REQUIRE(state.longTask.title.empty());
    REQUIRE(state.longTask.detail.empty());
}

TEST_CASE("Long task updates detail and progress", "[long-task]")
{
    cupuacu::test::StateWithTestPaths state{};
    cupuacu::LongTaskScope scope(&state, "Opening file", "0%",
                                 std::nullopt, false);

    cupuacu::updateLongTask(&state, "50%", 0.5, false);

    REQUIRE(state.longTask.active);
    REQUIRE(state.longTask.title == "Opening file");
    REQUIRE(state.longTask.detail == "50%");
    REQUIRE(state.longTask.progress.has_value());
    REQUIRE(*state.longTask.progress == 0.5);
}

TEST_CASE("Long task overlay consumes mouse input while visible", "[long-task]")
{
    cupuacu::test::StateWithTestPaths state{};
    cupuacu::gui::LongTaskOverlay overlay(&state);
    overlay.setBounds(0, 0, 400, 300);

    cupuacu::LongTaskScope scope(&state, "Opening file", "large.wav",
                                 std::nullopt, false);
    overlay.syncToState();

    const cupuacu::gui::MouseEvent down{
        cupuacu::gui::DOWN, 10, 10, 10.0f, 10.0f, 0.0f, 0.0f,
        cupuacu::gui::MouseButtonState{true, false, false}, 1};
    const cupuacu::gui::MouseEvent move{
        cupuacu::gui::MOVE, 12, 12, 12.0f, 12.0f, 2.0f, 2.0f,
        cupuacu::gui::MouseButtonState{false, false, false}, 0};
    const cupuacu::gui::MouseEvent wheel{
        cupuacu::gui::WHEEL, 12, 12, 12.0f, 12.0f, 0.0f, 0.0f,
        cupuacu::gui::MouseButtonState{false, false, false}, 0, 0.0f, 1.0f};

    REQUIRE(overlay.isVisible());
    REQUIRE(overlay.mouseDown(down));
    REQUIRE(overlay.mouseMove(move));
    REQUIRE(overlay.mouseWheel(wheel));
    REQUIRE_FALSE(overlay.shouldCaptureMouse());
}
