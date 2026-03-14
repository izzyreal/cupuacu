#include "Window.hpp"

#include "../State.hpp"
#include "MenuBar.hpp"
#include "WindowEventHandlingPlan.hpp"
#include "WindowEventPlanning.hpp"
#include "WindowMouseRouting.hpp"
#include "WindowResizePlanning.hpp"
#include "text.hpp"

#include <cmath>

using namespace cupuacu::gui;

namespace
{
    float getEffectiveWindowDisplayScale(SDL_Window *window)
    {
        if (!window)
        {
            return 1.0f;
        }
        const float scale = SDL_GetWindowDisplayScale(window);
        if (scale <= 0.0f)
        {
            return 1.0f;
        }
        return scale;
    }
}

Window::Window(State *stateToUse, const std::string &title, const int width,
               const int height, const Uint32 flags)
    : state(stateToUse)
{
    if (!SDL_CreateWindowAndRenderer(title.c_str(), width, height, flags,
                                     &window, &renderer))
    {
        SDL_Log("SDL_CreateWindowAndRenderer() failed: %s", SDL_GetError());
        return;
    }

    if (!SDL_SetRenderVSync(renderer, 1))
    {
        SDL_Log("SDL_SetRenderVSync(1) failed: %s", SDL_GetError());
    }

    windowId = SDL_GetWindowID(window);
    setFontDisplayScale(getEffectiveWindowDisplayScale(window));
    resizeCanvasIfNeeded();
}

Window::~Window()
{
    close();
}

bool Window::hasFocus() const
{
    if (!window)
    {
        return false;
    }

    return SDL_GetKeyboardFocus() == window;
}

void Window::setRootComponent(std::unique_ptr<Component> rootToUse)
{
    rootComponent = std::move(rootToUse);
    contentLayer = nullptr;
    overlayLayer = nullptr;
    menuBar = nullptr;
    capturingComponent = nullptr;
    componentUnderMouse = nullptr;
    if (rootComponent)
    {
        rootComponent->setWindow(this);
        rootComponent->setVisible(true);
    }
}

SDL_Point Window::computeRequiredCanvasDimensions() const
{
    if (!window)
    {
        return {0, 0};
    }

    SDL_Point pixelSize{0, 0};
    if (!SDL_GetWindowSizeInPixels(window, &pixelSize.x, &pixelSize.y))
    {
        return {0, 0};
    }

    SDL_Point logicalSize{0, 0};
    if (SDL_GetWindowSize(window, &logicalSize.x, &logicalSize.y) &&
        logicalSize.x > 0 && logicalSize.y > 0)
    {
        const float displayScale = getEffectiveWindowDisplayScale(window);
        pixelSize.x = std::max(
            pixelSize.x,
            static_cast<int>(std::lround(logicalSize.x * displayScale)));
        pixelSize.y = std::max(
            pixelSize.y,
            static_cast<int>(std::lround(logicalSize.y * displayScale)));
    }

    return planWindowCanvasDimensions(pixelSize.x, pixelSize.y,
                                      state->pixelScale);
}

void Window::resizeCanvasIfNeeded()
{
    if (!renderer)
    {
        return;
    }

    const SDL_Point required = computeRequiredCanvasDimensions();
    if (required.x <= 0 || required.y <= 0)
    {
        return;
    }

    if (canvas)
    {
        float currentW = 0.0f, currentH = 0.0f;
        SDL_GetTextureSize(canvas, &currentW, &currentH);
        if (!shouldRecreateWindowCanvas(static_cast<int>(currentW),
                                        static_cast<int>(currentH), required))
        {
            return;
        }
        SDL_DestroyTexture(canvas);
        canvas = nullptr;
    }

    canvas =
        SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888,
                          SDL_TEXTUREACCESS_TARGET, required.x, required.y);
    SDL_SetTextureScaleMode(canvas, SDL_SCALEMODE_NEAREST);
}

void Window::handleResize()
{
    if (!window)
    {
        return;
    }

    setFontDisplayScale(getEffectiveWindowDisplayScale(window));

    int winW = 0, winH = 0;
    SDL_GetWindowSize(window, &winW, &winH);

    const auto plan =
        planWindowResize(winW, winH, state->pixelScale, wasMaximized);
    if (!plan.valid)
    {
        return;
    }

    if (plan.requiresWindowResize)
    {
        if (plan.restoreFromMaximized)
        {
            wasMaximized = false;
            SDL_RestoreWindow(window);
            SDL_SetWindowSize(window, plan.targetWindowWidth,
                              plan.targetWindowHeight);
            SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED,
                                  SDL_WINDOWPOS_CENTERED);
        }
        else
        {
            SDL_SetWindowSize(window, plan.targetWindowWidth,
                              plan.targetWindowHeight);
        }
        return;
    }

    resizeCanvasIfNeeded();
    if (onResize)
    {
        onResize();
    }
}

void Window::refreshForScaleOrResize()
{
    setFontDisplayScale(getEffectiveWindowDisplayScale(window));
    resizeCanvasIfNeeded();
    if (onResize)
    {
        onResize();
    }
}

void Window::close()
{
    if (canvas)
    {
        SDL_DestroyTexture(canvas);
        canvas = nullptr;
    }
    if (renderer)
    {
        SDL_DestroyRenderer(renderer);
        renderer = nullptr;
    }
    if (window)
    {
        SDL_DestroyWindow(window);
        window = nullptr;
    }
    windowId = 0;
}

bool Window::isEventForWindow(const SDL_Event &event) const
{
    if (!window || windowId == 0)
    {
        return false;
    }

    const auto eventWindowId = getWindowEventWindowId(event);
    return eventWindowId.has_value() && *eventWindowId == windowId;
}

MouseEvent Window::makeMouseEvent(const SDL_Event &event) const
{
    auto draft = draftWindowMouseEvent(event);

    if (canvas && window)
    {
        SDL_FPoint canvasDimensions{0.0f, 0.0f};
        SDL_GetTextureSize(canvas, &canvasDimensions.x, &canvasDimensions.y);

        SDL_Point winDimensions{0, 0};
        SDL_GetWindowSize(window, &winDimensions.x, &winDimensions.y);

        if (winDimensions.x > 0 && winDimensions.y > 0)
        {
            scaleWindowMouseEventDraft(draft, canvasDimensions.x,
                                       canvasDimensions.y, winDimensions.x,
                                       winDimensions.y);
        }
    }

    return finalizeWindowMouseEvent(draft);
}

void Window::updateComponentUnderMouse(const int32_t mouseX,
                                       const int32_t mouseY)
{
    if (!rootComponent)
    {
        return;
    }

    const auto oldComponentUnderMouse = componentUnderMouse;
    const auto newComponentUnderMouse =
        rootComponent->findComponentAt(mouseX, mouseY);

    if (oldComponentUnderMouse != newComponentUnderMouse)
    {
        componentUnderMouse = newComponentUnderMouse;

        if (oldComponentUnderMouse != nullptr)
        {
            oldComponentUnderMouse->mouseLeave();
        }

        if (newComponentUnderMouse != nullptr)
        {
            newComponentUnderMouse->mouseEnter();
        }
    }
}

bool Window::handleEvent(const SDL_Event &event)
{
    if (!isEventForWindow(event))
    {
        return false;
    }

    const auto plan = planWindowEventHandling(event.type, rootComponent != nullptr);
    if (!plan.handled)
    {
        return true;
    }

    if (plan.markMaximized)
    {
        wasMaximized = true;
    }
    if (plan.handleResize)
    {
        handleResize();
    }
    if (plan.renderFrame)
    {
        renderFrame();
    }
    if (plan.invokeOnClose && onClose)
    {
        onClose();
    }
    if (plan.closeWindow)
    {
        close();
    }
    if (plan.forwardAsMouse)
    {
        handleMouseEvent(makeMouseEvent(event));
    }

    return true;
}

bool Window::handleMouseEvent(const MouseEvent &mouseEvent)
{
    const bool hasCapturingComponent = capturingComponent != nullptr;
    const bool captureContainsPoint =
        hasCapturingComponent &&
        capturingComponent->containsAbsoluteCoordinate(mouseEvent.mouseXi,
                                                      mouseEvent.mouseYi);
    const auto plan = planWindowMouseRouting(
        mouseEvent.type, rootComponent != nullptr, hasCapturingComponent,
        captureContainsPoint);

    if (!plan.handled)
    {
        return false;
    }

    if (plan.updateHoverBeforeDispatch)
    {
        updateComponentUnderMouse(mouseEvent.mouseXi, mouseEvent.mouseYi);
    }

    if (plan.sendLeaveToCaptureBeforeDispatch)
    {
        capturingComponent->mouseLeave();
    }

    if (plan.dispatchToRoot)
    {
        rootComponent->handleMouseEvent(mouseEvent);
    }

    if (plan.updateHoverAfterDispatch)
    {
        updateComponentUnderMouse(mouseEvent.mouseXi, mouseEvent.mouseYi);
    }

    if (plan.clearCaptureAfterDispatch)
    {
        capturingComponent = nullptr;
    }

    return true;
}

void Window::renderFrame()
{
    if (!renderer || !rootComponent || !canvas)
    {
        return;
    }

    dirtyRects.clear();
    SDL_Rect fullBounds{0, 0, 0, 0};
    if (canvas)
    {
        float canvasW = 0.0f, canvasH = 0.0f;
        SDL_GetTextureSize(canvas, &canvasW, &canvasH);
        fullBounds.w = (int)canvasW;
        fullBounds.h = (int)canvasH;
    }
    dirtyRects.push_back(fullBounds);

    rootComponent->setDirty();

    SDL_SetRenderTarget(renderer, canvas);
    rootComponent->draw(renderer);
    SDL_SetRenderTarget(renderer, nullptr);
    SDL_RenderTexture(renderer, canvas, nullptr, nullptr);
    SDL_RenderPresent(renderer);
    dirtyRects.clear();
}

void Window::renderFrameIfDirty()
{
    if (!renderer || !rootComponent || !canvas || dirtyRects.empty())
    {
        return;
    }

    // Overlay must repaint whenever anything below changes so popups stay on
    // top even when underlying content (e.g. waveforms) is animating.
    if (overlayLayer)
    {
        overlayLayer->setDirty();
    }

    SDL_SetRenderTarget(renderer, canvas);
    rootComponent->draw(renderer);
    SDL_SetRenderTarget(renderer, nullptr);
    SDL_RenderTexture(renderer, canvas, nullptr, nullptr);
    SDL_RenderPresent(renderer);
    dirtyRects.clear();
}
