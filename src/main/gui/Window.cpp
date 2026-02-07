#include "Window.hpp"

#include "../State.hpp"
#include "MenuBar.hpp"

#include <cmath>

using namespace cupuacu::gui;

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

    windowId = SDL_GetWindowID(window);
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
    SDL_Point result{0, 0};
    if (!window)
    {
        return result;
    }

    if (!SDL_GetWindowSizeInPixels(window, &result.x, &result.y))
    {
        return {0, 0};
    }

    result.x = std::floor(result.x / state->pixelScale);
    result.y = std::floor(result.y / state->pixelScale);

    return result;
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
        if ((int)currentW == required.x && (int)currentH == required.y)
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

    int winW = 0, winH = 0;
    SDL_GetWindowSize(window, &winW, &winH);

    const int hpp = state->pixelScale;
    const int newW = winW / hpp * hpp;
    const int newH = winH / hpp * hpp;

    if (newW != winW || newH != winH)
    {
        if (wasMaximized)
        {
            wasMaximized = false;
            SDL_RestoreWindow(window);
            SDL_SetWindowSize(window, newW, newH);
            SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED,
                                  SDL_WINDOWPOS_CENTERED);
        }
        else
        {
            SDL_SetWindowSize(window, newW, newH);
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

    SDL_WindowID eventWindowId = 0;
    switch (event.type)
    {
        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_MAXIMIZED:
        case SDL_EVENT_WINDOW_EXPOSED:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            eventWindowId = event.window.windowID;
            break;
        case SDL_EVENT_MOUSE_MOTION:
            eventWindowId = event.motion.windowID;
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            eventWindowId = event.button.windowID;
            break;
        default:
            return false;
    }

    return eventWindowId == windowId;
}

MouseEvent Window::makeMouseEvent(const SDL_Event &event) const
{
    MouseEventType type = MOVE;
    float xf = 0.0f, yf = 0.0f;
    float relx = 0.0f, rely = 0.0f;
    int32_t xi = 0, yi = 0;
    bool left = false, middle = false, right = false;
    uint8_t clicks = 0;

    if (event.type == SDL_EVENT_MOUSE_MOTION)
    {
        type = MOVE;
        xf = event.motion.x;
        yf = event.motion.y;
        relx = event.motion.xrel;
        rely = event.motion.yrel;
        left = (event.motion.state & SDL_BUTTON_LMASK) != 0;
        middle = (event.motion.state & SDL_BUTTON_MMASK) != 0;
        right = (event.motion.state & SDL_BUTTON_RMASK) != 0;
    }
    else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
             event.type == SDL_EVENT_MOUSE_BUTTON_UP)
    {
        type = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? DOWN : UP;
        xf = event.button.x;
        yf = event.button.y;
        left = event.button.button == SDL_BUTTON_LEFT;
        middle = event.button.button == SDL_BUTTON_MIDDLE;
        right = event.button.button == SDL_BUTTON_RIGHT;
        clicks = event.button.clicks;
    }

    if (canvas && window)
    {
        SDL_FPoint canvasDimensions{0.0f, 0.0f};
        SDL_GetTextureSize(canvas, &canvasDimensions.x, &canvasDimensions.y);

        SDL_Point winDimensions{0, 0};
        SDL_GetWindowSize(window, &winDimensions.x, &winDimensions.y);

        if (winDimensions.x > 0 && winDimensions.y > 0)
        {
            xf *= canvasDimensions.x / winDimensions.x;
            yf *= canvasDimensions.y / winDimensions.y;
            relx *= canvasDimensions.x / winDimensions.x;
            rely *= canvasDimensions.y / winDimensions.y;
        }
    }

    xi = static_cast<int32_t>(std::floor(xf));
    yi = static_cast<int32_t>(std::floor(yf));

    const MouseButtonState bs{left, middle, right};
    return MouseEvent{type, xi, yi, xf, yf, relx, rely, bs, clicks};
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

    switch (event.type)
    {
        case SDL_EVENT_WINDOW_MAXIMIZED:
            wasMaximized = true;
            break;
        case SDL_EVENT_WINDOW_RESIZED:
            handleResize();
            break;
        case SDL_EVENT_WINDOW_EXPOSED:
            renderFrame();
            break;
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            if (onClose)
            {
                onClose();
            }
            close();
            break;
        case SDL_EVENT_MOUSE_MOTION:
        {
            if (!rootComponent)
            {
                break;
            }
            const MouseEvent mouseEvent = makeMouseEvent(event);
            rootComponent->handleMouseEvent(mouseEvent);

            if (capturingComponent == nullptr)
            {
                updateComponentUnderMouse(mouseEvent.mouseXi,
                                          mouseEvent.mouseYi);
            }
            break;
        }
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        {
            if (!rootComponent)
            {
                break;
            }
            const MouseEvent mouseEvent = makeMouseEvent(event);
            if (capturingComponent == nullptr)
            {
                rootComponent->handleMouseEvent(mouseEvent);
            }
            else
            {
                capturingComponent->handleMouseEvent(mouseEvent);
            }
            break;
        }
        case SDL_EVENT_MOUSE_BUTTON_UP:
        {
            if (!rootComponent)
            {
                break;
            }
            const MouseEvent mouseEvent = makeMouseEvent(event);
            updateComponentUnderMouse(mouseEvent.mouseXi, mouseEvent.mouseYi);

            if (capturingComponent == nullptr)
            {
                rootComponent->handleMouseEvent(mouseEvent);
            }
            else
            {
                if (!capturingComponent->containsAbsoluteCoordinate(
                        mouseEvent.mouseXi, mouseEvent.mouseYi))
                {
                    capturingComponent->mouseLeave();
                }
                capturingComponent->handleMouseEvent(mouseEvent);
                capturingComponent = nullptr;
            }
            break;
        }
        default:
            break;
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
