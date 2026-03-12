#include <catch2/catch_test_macros.hpp>

#include "gui/WindowEventHandlingPlan.hpp"
#include "gui/WindowEventPlanning.hpp"
#include "gui/WindowMouseRouting.hpp"
#include "gui/WindowResizePlanning.hpp"

TEST_CASE("Window mouse routing helper plans hover and capture transitions",
          "[gui]")
{
    SECTION("move without capture refreshes hover after dispatch")
    {
        const auto plan = cupuacu::gui::planWindowMouseRouting(
            cupuacu::gui::MOVE, true, false, false);

        REQUIRE(plan.handled);
        REQUIRE(plan.dispatchToRoot);
        REQUIRE_FALSE(plan.updateHoverBeforeDispatch);
        REQUIRE(plan.updateHoverAfterDispatch);
        REQUIRE_FALSE(plan.sendLeaveToCaptureBeforeDispatch);
        REQUIRE_FALSE(plan.clearCaptureAfterDispatch);
    }

    SECTION("move with capture skips hover refresh")
    {
        const auto plan = cupuacu::gui::planWindowMouseRouting(
            cupuacu::gui::MOVE, true, true, true);

        REQUIRE(plan.handled);
        REQUIRE(plan.dispatchToRoot);
        REQUIRE_FALSE(plan.updateHoverBeforeDispatch);
        REQUIRE_FALSE(plan.updateHoverAfterDispatch);
        REQUIRE_FALSE(plan.sendLeaveToCaptureBeforeDispatch);
        REQUIRE_FALSE(plan.clearCaptureAfterDispatch);
    }

    SECTION("mouse up refreshes hover and releases capture")
    {
        const auto plan = cupuacu::gui::planWindowMouseRouting(
            cupuacu::gui::UP, true, true, false);

        REQUIRE(plan.handled);
        REQUIRE(plan.dispatchToRoot);
        REQUIRE(plan.updateHoverBeforeDispatch);
        REQUIRE_FALSE(plan.updateHoverAfterDispatch);
        REQUIRE(plan.sendLeaveToCaptureBeforeDispatch);
        REQUIRE(plan.clearCaptureAfterDispatch);
    }

    SECTION("mouse up does not synthesize leave when capture still contains cursor")
    {
        const auto plan = cupuacu::gui::planWindowMouseRouting(
            cupuacu::gui::UP, true, true, true);

        REQUIRE(plan.handled);
        REQUIRE(plan.dispatchToRoot);
        REQUIRE(plan.updateHoverBeforeDispatch);
        REQUIRE_FALSE(plan.sendLeaveToCaptureBeforeDispatch);
        REQUIRE(plan.clearCaptureAfterDispatch);
    }

    SECTION("wheel refreshes hover before dispatch")
    {
        const auto plan = cupuacu::gui::planWindowMouseRouting(
            cupuacu::gui::WHEEL, true, false, false);

        REQUIRE(plan.handled);
        REQUIRE(plan.dispatchToRoot);
        REQUIRE(plan.updateHoverBeforeDispatch);
        REQUIRE_FALSE(plan.updateHoverAfterDispatch);
        REQUIRE_FALSE(plan.sendLeaveToCaptureBeforeDispatch);
        REQUIRE_FALSE(plan.clearCaptureAfterDispatch);
    }

    SECTION("no root means event is ignored")
    {
        const auto plan = cupuacu::gui::planWindowMouseRouting(
            cupuacu::gui::DOWN, false, false, false);

        REQUIRE_FALSE(plan.handled);
        REQUIRE_FALSE(plan.dispatchToRoot);
        REQUIRE_FALSE(plan.updateHoverBeforeDispatch);
        REQUIRE_FALSE(plan.updateHoverAfterDispatch);
        REQUIRE_FALSE(plan.sendLeaveToCaptureBeforeDispatch);
        REQUIRE_FALSE(plan.clearCaptureAfterDispatch);
    }
}

TEST_CASE("Window event planning extracts window IDs and mouse events", "[gui]")
{
    SDL_Event motion{};
    motion.type = SDL_EVENT_MOUSE_MOTION;
    motion.motion.windowID = 42;
    motion.motion.x = 10.5f;
    motion.motion.y = 20.25f;
    motion.motion.xrel = 3.0f;
    motion.motion.yrel = -4.0f;
    motion.motion.state = SDL_BUTTON_LMASK;

    const auto motionWindowId = cupuacu::gui::getWindowEventWindowId(motion);
    REQUIRE(motionWindowId.has_value());
    REQUIRE(*motionWindowId == 42);

    auto draft = cupuacu::gui::draftWindowMouseEvent(motion);
    REQUIRE(draft.valid);
    REQUIRE(draft.type == cupuacu::gui::MOVE);
    REQUIRE(draft.left);
    REQUIRE_FALSE(draft.middle);
    REQUIRE_FALSE(draft.right);

    cupuacu::gui::scaleWindowMouseEventDraft(draft, 400.0f, 200.0f, 200, 100);
    const auto event = cupuacu::gui::finalizeWindowMouseEvent(draft);
    REQUIRE(event.mouseXi == 21);
    REQUIRE(event.mouseYi == 40);
    REQUIRE(event.mouseXf == 21.0f);
    REQUIRE(event.mouseYf == 40.5f);
    REQUIRE(event.mouseRelX == 6.0f);
    REQUIRE(event.mouseRelY == -8.0f);
    REQUIRE(event.buttonState.left);
}

TEST_CASE("Window event planning supports button, wheel, and unsupported events",
          "[gui]")
{
    SDL_Event button{};
    button.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    button.button.windowID = 9;
    button.button.x = 11.0f;
    button.button.y = 12.0f;
    button.button.button = SDL_BUTTON_RIGHT;
    button.button.clicks = 2;

    const auto buttonWindowId = cupuacu::gui::getWindowEventWindowId(button);
    REQUIRE(buttonWindowId.has_value());
    REQUIRE(*buttonWindowId == 9);

    const auto buttonDraft = cupuacu::gui::draftWindowMouseEvent(button);
    REQUIRE(buttonDraft.valid);
    REQUIRE(buttonDraft.type == cupuacu::gui::DOWN);
    REQUIRE(buttonDraft.right);
    REQUIRE(buttonDraft.clicks == 2);

    SDL_Event wheel{};
    wheel.type = SDL_EVENT_MOUSE_WHEEL;
    wheel.wheel.windowID = 7;
    wheel.wheel.mouse_x = 13.0f;
    wheel.wheel.mouse_y = 14.0f;
    wheel.wheel.x = -1.0f;
    wheel.wheel.y = 2.0f;

    const auto wheelWindowId = cupuacu::gui::getWindowEventWindowId(wheel);
    REQUIRE(wheelWindowId.has_value());
    REQUIRE(*wheelWindowId == 7);

    const auto wheelDraft = cupuacu::gui::draftWindowMouseEvent(wheel);
    REQUIRE(wheelDraft.valid);
    REQUIRE(wheelDraft.type == cupuacu::gui::WHEEL);
    REQUIRE(wheelDraft.wheelX == -1.0f);
    REQUIRE(wheelDraft.wheelY == 2.0f);

    SDL_Event unsupported{};
    unsupported.type = SDL_EVENT_KEY_DOWN;
    REQUIRE_FALSE(cupuacu::gui::getWindowEventWindowId(unsupported).has_value());
    REQUIRE_FALSE(cupuacu::gui::draftWindowMouseEvent(unsupported).valid);
}

TEST_CASE("Window event handling plan covers window and mouse event branches",
          "[gui]")
{
    SECTION("window lifecycle events map to dedicated actions")
    {
        const auto maximized = cupuacu::gui::planWindowEventHandling(
            SDL_EVENT_WINDOW_MAXIMIZED, true);
        REQUIRE(maximized.handled);
        REQUIRE(maximized.markMaximized);
        REQUIRE_FALSE(maximized.handleResize);

        const auto resized = cupuacu::gui::planWindowEventHandling(
            SDL_EVENT_WINDOW_RESIZED, true);
        REQUIRE(resized.handled);
        REQUIRE(resized.handleResize);
        REQUIRE_FALSE(resized.renderFrame);

        const auto exposed = cupuacu::gui::planWindowEventHandling(
            SDL_EVENT_WINDOW_EXPOSED, true);
        REQUIRE(exposed.handled);
        REQUIRE(exposed.renderFrame);

        const auto closeRequested = cupuacu::gui::planWindowEventHandling(
            SDL_EVENT_WINDOW_CLOSE_REQUESTED, true);
        REQUIRE(closeRequested.handled);
        REQUIRE(closeRequested.invokeOnClose);
        REQUIRE(closeRequested.closeWindow);
    }

    SECTION("mouse events require a root component")
    {
        const auto withRoot = cupuacu::gui::planWindowEventHandling(
            SDL_EVENT_MOUSE_BUTTON_DOWN, true);
        REQUIRE(withRoot.handled);
        REQUIRE(withRoot.forwardAsMouse);

        const auto withoutRoot = cupuacu::gui::planWindowEventHandling(
            SDL_EVENT_MOUSE_WHEEL, false);
        REQUIRE_FALSE(withoutRoot.handled);
        REQUIRE_FALSE(withoutRoot.forwardAsMouse);
    }

    SECTION("unsupported events stay ignored")
    {
        const auto unsupported = cupuacu::gui::planWindowEventHandling(
            SDL_EVENT_KEY_DOWN, true);
        REQUIRE_FALSE(unsupported.handled);
        REQUIRE_FALSE(unsupported.markMaximized);
        REQUIRE_FALSE(unsupported.forwardAsMouse);
    }
}

TEST_CASE("Window resize planning normalizes canvas and resize policy", "[gui]")
{
    SECTION("canvas dimensions follow pixel scale")
    {
        const auto canvas = cupuacu::gui::planWindowCanvasDimensions(801, 603, 3);
        REQUIRE(canvas.x == 267);
        REQUIRE(canvas.y == 201);
        REQUIRE(cupuacu::gui::shouldRecreateWindowCanvas(266, 201, canvas));
        REQUIRE_FALSE(
            cupuacu::gui::shouldRecreateWindowCanvas(267, 201, canvas));
    }

    SECTION("resize plan snaps to pixel scale and notes maximized restore")
    {
        const auto plan = cupuacu::gui::planWindowResize(803, 605, 3, true);
        REQUIRE(plan.valid);
        REQUIRE(plan.requiresWindowResize);
        REQUIRE(plan.restoreFromMaximized);
        REQUIRE(plan.targetWindowWidth == 801);
        REQUIRE(plan.targetWindowHeight == 603);
    }

    SECTION("already aligned size only refreshes canvas")
    {
        const auto plan = cupuacu::gui::planWindowResize(800, 600, 2, false);
        REQUIRE(plan.valid);
        REQUIRE_FALSE(plan.requiresWindowResize);
        REQUIRE_FALSE(plan.restoreFromMaximized);
    }
}
