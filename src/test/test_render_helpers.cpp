#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "gui/Helpers.hpp"
#include "gui/LabelPlanning.hpp"
#include "gui/RoundedRect.hpp"
#include "gui/RoundedRectPlanning.hpp"
#include "gui/TextPlanning.hpp"
#include "gui/text.hpp"

#include <SDL3/SDL.h>

TEST_CASE("RoundedRect planning clamps radius and computes core rects", "[gui]")
{
    const SDL_FRect rect{10.0f, 20.0f, 30.0f, 12.0f};

    REQUIRE(cupuacu::gui::clampRoundedRectRadius(rect, -1.0f) ==
            Catch::Approx(0.0f));
    REQUIRE(cupuacu::gui::clampRoundedRectRadius(rect, 20.0f) ==
            Catch::Approx(6.0f));

    const auto geometry = cupuacu::gui::planRoundedRectGeometry(rect, 20.0f);
    REQUIRE(geometry.radius == Catch::Approx(6.0f));
    REQUIRE(geometry.x0 == Catch::Approx(10.0f));
    REQUIRE(geometry.y0 == Catch::Approx(20.0f));
    REQUIRE(geometry.x1 == Catch::Approx(39.0f));
    REQUIRE(geometry.y1 == Catch::Approx(31.0f));

    const auto core = cupuacu::gui::planRoundedRectCore(rect, geometry.radius);
    REQUIRE(core.x == Catch::Approx(16.0f));
    REQUIRE(core.w == Catch::Approx(18.0f));

    const auto vertical =
        cupuacu::gui::planRoundedRectVerticalCore(rect, geometry.radius);
    REQUIRE(vertical.y == Catch::Approx(26.0f));
    REQUIRE(vertical.h == Catch::Approx(0.0f));
}

TEST_CASE("Text planning centers safely", "[gui]")
{
    const SDL_FRect rect{10.0f, 5.0f, 20.0f, 8.0f};
    REQUIRE(cupuacu::gui::planTextXPosition(rect, 8, false) ==
            Catch::Approx(10.0f));
    REQUIRE(cupuacu::gui::planTextXPosition(rect, 8, true) ==
            Catch::Approx(16.0f));
    REQUIRE(cupuacu::gui::planTextXPosition(rect, 40, true) ==
            Catch::Approx(10.0f));
}

TEST_CASE("Label planning derives rebuild and destination layout decisions",
          "[gui]")
{
    REQUIRE(cupuacu::gui::shouldRebuildLabelTexture(
        nullptr, "", "Text", 0, 12, 0, 255));
    REQUIRE_FALSE(cupuacu::gui::shouldRebuildLabelTexture(
        reinterpret_cast<SDL_Texture *>(0x1), "Text", "Text", 12, 12, 255, 255));
    REQUIRE(cupuacu::gui::shouldRebuildLabelTexture(
        reinterpret_cast<SDL_Texture *>(0x1), "Old", "Text", 12, 12, 255, 255));

    const SDL_FRect bounds{0.0f, 0.0f, 40.0f, 20.0f};
    const auto content =
        cupuacu::gui::planLabelContentRect(bounds, 2.0f, true, 8);
    REQUIRE(content.x == Catch::Approx(2.0f));
    REQUIRE(content.y == Catch::Approx(6.0f));
    REQUIRE(content.w == Catch::Approx(36.0f));
    REQUIRE(content.h == Catch::Approx(8.0f));

    const auto centered =
        cupuacu::gui::planLabelDestRect(content, 10, 8, true, 1);
    REQUIRE(centered.x == Catch::Approx(15.0f));
    REQUIRE(centered.y == Catch::Approx(6.0f));

    const auto snapped =
        cupuacu::gui::planLabelDestRect(content, 10, 8, true, 2);
    REQUIRE(snapped.x == Catch::Approx(15.0f));
    REQUIRE(snapped.y == Catch::Approx(6.0f));
}

TEST_CASE("Helpers geometry utilities cover subtract and fill helpers", "[gui]")
{
    const SDL_Rect a{0, 0, 10, 10};
    const SDL_Rect b{5, 0, 5, 10};
    REQUIRE(Helpers::intersects(a, b));

    const auto f = Helpers::rectToFRect(a);
    REQUIRE(f.w == Catch::Approx(10.0f));

    const SDL_Rect leftClipped = Helpers::subtractRect(a, SDL_Rect{0, 0, 4, 10});
    REQUIRE(leftClipped.x == 4);
    REQUIRE(leftClipped.w == 6);

    const SDL_Rect rightClipped =
        Helpers::subtractRect(a, SDL_Rect{6, 0, 4, 10});
    REQUIRE(rightClipped.x == 0);
    REQUIRE(rightClipped.w == 6);

    const SDL_Rect fullyClipped =
        Helpers::subtractRect(a, SDL_Rect{0, 0, 10, 10});
    REQUIRE(fullyClipped.w == 0);
}
