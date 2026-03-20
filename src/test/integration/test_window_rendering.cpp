#include <catch2/catch_test_macros.hpp>

#include "IntegrationTestHelpers.hpp"

#include "State.hpp"
#include "gui/DevicePropertiesWindow.hpp"
#include "gui/Window.hpp"
#include "gui/text.hpp"

#include <SDL3/SDL.h>

#include <memory>

namespace
{
    class DrawCountingComponent : public cupuacu::gui::Component
    {
    public:
        explicit DrawCountingComponent(cupuacu::State *state,
                                       const char *name = "DrawCounter")
            : Component(state, name)
        {
        }

        int drawCount = 0;

        void onDraw(SDL_Renderer *) override
        {
            ++drawCount;
        }
    };
} // namespace

TEST_CASE("Window rendering integration recreates canvas when pixel scale changes",
          "[integration]")
{
    cupuacu::test::ensureSdlTtfInitialized();

    cupuacu::State state{};
    state.pixelScale = 1;

    auto window = std::make_unique<cupuacu::gui::Window>(
        &state, "window-render-scale", 320, 180, SDL_WINDOW_HIDDEN);

    auto root =
        std::make_unique<cupuacu::test::integration::RootComponent>(&state);
    root->setBounds(0, 0, 320, 180);
    window->setRootComponent(std::move(root));

    REQUIRE(window->getCanvas() != nullptr);

    float initialCanvasW = 0.0f;
    float initialCanvasH = 0.0f;
    SDL_GetTextureSize(window->getCanvas(), &initialCanvasW, &initialCanvasH);
    REQUIRE(initialCanvasW == 320.0f);
    REQUIRE(initialCanvasH == 180.0f);

    state.pixelScale = 2;
    window->refreshForScaleOrResize();

    REQUIRE(window->getCanvas() != nullptr);
    float resizedCanvasW = 0.0f;
    float resizedCanvasH = 0.0f;
    SDL_GetTextureSize(window->getCanvas(), &resizedCanvasW, &resizedCanvasH);
    REQUIRE(resizedCanvasW == 160.0f);
    REQUIRE(resizedCanvasH == 90.0f);
}

TEST_CASE("Window rendering integration redraws only dirty frames",
          "[integration]")
{
    cupuacu::test::ensureSdlTtfInitialized();

    cupuacu::State state{};
    auto window = std::make_unique<cupuacu::gui::Window>(
        &state, "window-render-dirty", 320, 180, SDL_WINDOW_HIDDEN);

    auto root =
        std::make_unique<cupuacu::test::integration::RootComponent>(&state);
    auto *counter = root->emplaceChild<DrawCountingComponent>(&state);
    root->setBounds(0, 0, 320, 180);
    counter->setBounds(0, 0, 320, 180);
    window->setRootComponent(std::move(root));

    window->renderFrame();
    REQUIRE(counter->drawCount == 1);
    REQUIRE(window->getDirtyRects().empty());

    window->renderFrameIfDirty();
    REQUIRE(counter->drawCount == 1);

    counter->setDirty();
    REQUIRE_FALSE(window->getDirtyRects().empty());

    window->renderFrameIfDirty();
    REQUIRE(counter->drawCount == 2);
    REQUIRE(window->getDirtyRects().empty());
}

TEST_CASE("Window rendering integration exposes font cache and text measurement",
          "[integration]")
{
    cupuacu::test::ensureSdlTtfInitialized();

    cupuacu::gui::cleanupFonts();
    REQUIRE_FALSE(cupuacu::gui::hasCachedFontForPointSize(12));

    auto *font = cupuacu::gui::getFont(12);
    REQUIRE(font != nullptr);
    REQUIRE(cupuacu::gui::hasCachedFontForPointSize(12));

    const auto size = cupuacu::gui::measureText("Hi", 12);
    REQUIRE(size.first > 0);
    REQUIRE(size.second > 0);

    cupuacu::gui::cleanupFonts();
    REQUIRE_FALSE(cupuacu::gui::hasCachedFontForPointSize(12));
}
