#include "Window.hpp"

#include "../ResourceUtil.hpp"
#include "../State.hpp"
#include "DropdownMenu.hpp"
#include "MenuBar.hpp"
#include "TooltipController.hpp"
#include "TooltipPlanning.hpp"
#include "WindowEventHandlingPlan.hpp"
#include "WindowEventPlanning.hpp"
#include "WindowMouseRouting.hpp"
#include "WindowResizePlanning.hpp"
#include "text.hpp"

#include <algorithm>
#include <cmath>

using namespace cupuacu::gui;

namespace
{
    bool shouldLogWindowScaleDiagnostics()
    {
        const char *value = SDL_getenv("CUPUACU_DEBUG_WINDOW_SCALE");
        return value && value[0] != '\0' && SDL_strcmp(value, "0") != 0;
    }

    float getEffectiveWindowDisplayScale(SDL_Window *window,
                                         SDL_Texture *canvas)
    {
        if (!window)
        {
            return 1.0f;
        }

        if (canvas)
        {
            SDL_Point logicalSize{0, 0};
            if (SDL_GetWindowSize(window, &logicalSize.x, &logicalSize.y) &&
                logicalSize.x > 0 && logicalSize.y > 0)
            {
                float canvasW = 0.0f, canvasH = 0.0f;
                SDL_GetTextureSize(canvas, &canvasW, &canvasH);
                if (canvasW > 0.0f && canvasH > 0.0f)
                {
                    const float scaleX = canvasW / logicalSize.x;
                    const float scaleY = canvasH / logicalSize.y;
                    const float scale = std::min(scaleX, scaleY);
                    if (scale > 0.0f)
                    {
                        return scale;
                    }
                }
            }
        }

        const float fallbackScale = SDL_GetWindowDisplayScale(window);
        if (fallbackScale <= 0.0f)
        {
            return 1.0f;
        }
        return fallbackScale;
    }

    void logWindowScaleDiagnostics(SDL_Window *window, SDL_Texture *canvas,
                                   const Uint8 pixelScale)
    {
        if (!shouldLogWindowScaleDiagnostics() || !window)
        {
            return;
        }

        SDL_Point logicalSize{0, 0};
        SDL_Point pixelSize{0, 0};
        SDL_GetWindowSize(window, &logicalSize.x, &logicalSize.y);
        SDL_GetWindowSizeInPixels(window, &pixelSize.x, &pixelSize.y);
        const SDL_DisplayID displayId = SDL_GetDisplayForWindow(window);
        const float displayContentScale =
            displayId ? SDL_GetDisplayContentScale(displayId) : 0.0f;

        float canvasW = 0.0f;
        float canvasH = 0.0f;
        if (canvas)
        {
            SDL_GetTextureSize(canvas, &canvasW, &canvasH);
        }

        SDL_Log(
            "CUPUACU_DEBUG_WINDOW_SCALE: logical=%dx%d pixels=%dx%d displayId=%" SDL_PRIu32 " displayScale=%.3f displayContentScale=%.3f canvas=%.1fx%.1f pixelScale=%u effectiveFontScale=%.3f",
            logicalSize.x, logicalSize.y, pixelSize.x, pixelSize.y,
            displayId, SDL_GetWindowDisplayScale(window), displayContentScale,
            canvasW, canvasH, pixelScale,
            getEffectiveWindowDisplayScale(window, canvas));
    }

    SDL_Surface *createWindowIconSurface()
    {
        static const std::string iconData =
            cupuacu::get_resource_data("cupuacu-logo1.bmp");
        if (iconData.empty())
        {
            SDL_Log("Failed to load bundled icon resource");
            return nullptr;
        }

        SDL_IOStream *stream =
            SDL_IOFromConstMem(iconData.data(), iconData.size());
        if (!stream)
        {
            SDL_Log("SDL_IOFromConstMem() failed for icon: %s",
                    SDL_GetError());
            return nullptr;
        }

        SDL_Surface *source = SDL_LoadBMP_IO(stream, true);
        if (!source)
        {
            SDL_Log("SDL_LoadBMP_IO() failed for icon: %s", SDL_GetError());
            return nullptr;
        }

        constexpr int iconSize = 256;
        const int upscaleFactor =
            std::max(1, iconSize / std::max(source->w, source->h));
        const int scaledWidth = source->w * upscaleFactor;
        const int scaledHeight = source->h * upscaleFactor;

        SDL_Surface *scaled =
            SDL_ScaleSurface(source, scaledWidth, scaledHeight,
                             SDL_SCALEMODE_NEAREST);
        SDL_DestroySurface(source);
        if (!scaled)
        {
            SDL_Log("SDL_ScaleSurface() failed for icon: %s", SDL_GetError());
            return nullptr;
        }

        SDL_Surface *canvas =
            SDL_CreateSurface(iconSize, iconSize, SDL_PIXELFORMAT_ARGB8888);
        if (!canvas)
        {
            SDL_Log("SDL_CreateSurface() failed for icon: %s", SDL_GetError());
            SDL_DestroySurface(scaled);
            return nullptr;
        }

        SDL_ClearSurface(canvas, 0.0f, 0.0f, 0.0f, 0.0f);
        const SDL_Rect destination{
            (iconSize - scaledWidth) / 2, (iconSize - scaledHeight) / 2,
            scaledWidth, scaledHeight};
        if (!SDL_BlitSurfaceScaled(scaled, nullptr, canvas, &destination,
                                   SDL_SCALEMODE_NEAREST))
        {
            SDL_Log("SDL_BlitSurfaceScaled() failed for icon: %s",
                    SDL_GetError());
            SDL_DestroySurface(canvas);
            SDL_DestroySurface(scaled);
            return nullptr;
        }

        SDL_DestroySurface(scaled);
        return canvas;
    }

    void applyWindowIcon(SDL_Window *window)
    {
        if (!window)
        {
            return;
        }

        SDL_Surface *icon = createWindowIconSurface();
        if (!icon)
        {
            return;
        }

        if (!SDL_SetWindowIcon(window, icon))
        {
            SDL_Log("SDL_SetWindowIcon() failed: %s", SDL_GetError());
        }
        SDL_DestroySurface(icon);
    }

    void collectExpandedDropdowns(Component *component,
                                  std::vector<DropdownMenu *> &dropdowns)
    {
        if (!component)
        {
            return;
        }

        if (auto *dropdown = dynamic_cast<DropdownMenu *>(component);
            dropdown != nullptr && dropdown->isExpanded())
        {
            dropdowns.push_back(dropdown);
        }

        for (const auto &child : component->getChildren())
        {
            collectExpandedDropdowns(child.get(), dropdowns);
        }
    }

    DropdownMenu *findAncestorDropdown(Component *component)
    {
        while (component != nullptr)
        {
            if (auto *dropdown = dynamic_cast<DropdownMenu *>(component))
            {
                return dropdown;
            }
            if (auto *ownerProvider =
                    dynamic_cast<DropdownOwnerComponent *>(component))
            {
                return ownerProvider->getOwningDropdown();
            }
            component = component->getParentComponent();
        }
        return nullptr;
    }

    void collectExpandedDropdownsFromWindow(Window *window,
                                            std::vector<DropdownMenu *> &dropdowns)
    {
        if (!window || !window->getRootComponent())
        {
            return;
        }

        collectExpandedDropdowns(window->getRootComponent(), dropdowns);
    }

    bool collapseExpandedDropdowns(Window *activeWindow, cupuacu::State *state,
                                   DropdownMenu *dropdownToKeepOpen)
    {
        std::vector<DropdownMenu *> expandedDropdowns;
        if (activeWindow)
        {
            collectExpandedDropdownsFromWindow(activeWindow, expandedDropdowns);
        }

        if (!state)
        {
            bool changed = false;
            for (auto *dropdown : expandedDropdowns)
            {
                if (dropdown == dropdownToKeepOpen)
                {
                    continue;
                }

                dropdown->setExpanded(false);
                changed = true;
            }
            return changed;
        }

        for (auto *candidateWindow : state->windows)
        {
            if (candidateWindow == activeWindow)
            {
                continue;
            }
            collectExpandedDropdownsFromWindow(candidateWindow, expandedDropdowns);
        }

        bool changed = false;
        for (auto *dropdown : expandedDropdowns)
        {
            if (dropdown == dropdownToKeepOpen)
            {
                continue;
            }

            dropdown->setExpanded(false);
            changed = true;
        }

        return changed;
    }
}

Window::Window(State *stateToUse, const std::string &title, const int width,
               const int height, const Uint32 flags)
    : state(stateToUse)
{
    transparentWindow = (flags & SDL_WINDOW_TRANSPARENT) != 0;
    if (!SDL_CreateWindowAndRenderer(title.c_str(), width, height, flags,
                                     &window, &renderer))
    {
        SDL_Log("SDL_CreateWindowAndRenderer() failed: %s", SDL_GetError());
        return;
    }

    applyWindowIcon(window);

    if (!SDL_SetRenderVSync(renderer, 1))
    {
        SDL_Log("SDL_SetRenderVSync(1) failed: %s", SDL_GetError());
    }
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    windowId = SDL_GetWindowID(window);
    resizeCanvasIfNeeded();
    setFontDisplayScale(getEffectiveWindowDisplayScale(window, canvas));
    tooltipController = std::make_unique<TooltipController>(state, this);
}

Window::Window(State *stateToUse, SDL_Window *parentWindow, const int offsetX,
               const int offsetY, const int width, const int height,
               const Uint32 flags)
    : state(stateToUse)
{
    popupWindow = true;
    transparentWindow = (flags & SDL_WINDOW_TRANSPARENT) != 0;
    window = SDL_CreatePopupWindow(parentWindow, offsetX, offsetY, width, height,
                                   flags);
    if (!window)
    {
        SDL_Log("SDL_CreatePopupWindow() failed: %s", SDL_GetError());
        return;
    }

    renderer = SDL_CreateRenderer(window, nullptr);
    if (!renderer)
    {
        SDL_Log("SDL_CreateRenderer() failed for popup window: %s",
                SDL_GetError());
        close();
        return;
    }

    if (!SDL_SetRenderVSync(renderer, 1))
    {
        SDL_Log("SDL_SetRenderVSync(1) failed: %s", SDL_GetError());
    }
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    windowId = SDL_GetWindowID(window);
    resizeCanvasIfNeeded();
    setFontDisplayScale(getEffectiveWindowDisplayScale(window, canvas));
    tooltipController = std::make_unique<TooltipController>(state, this);
}

Window::~Window()
{
    tooltipController.reset();
    rootComponent.reset();
    contentLayer = nullptr;
    overlayLayer = nullptr;
    menuBar = nullptr;
    capturingComponent = nullptr;
    componentUnderMouse = nullptr;
    focusedComponent = nullptr;
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
    hideTooltip();
    contentLayer = nullptr;
    overlayLayer = nullptr;
    menuBar = nullptr;
    capturingComponent = nullptr;
    componentUnderMouse = nullptr;
    focusedComponent = nullptr;
    if (rootComponent)
    {
        rootComponent->setWindow(this);
        rootComponent->setVisible(true);
    }
}

bool Window::setCanvasSize(const int width, const int height)
{
    if (!renderer || width <= 0 || height <= 0)
    {
        return false;
    }

    if (canvas)
    {
        float currentW = 0.0f;
        float currentH = 0.0f;
        SDL_GetTextureSize(canvas, &currentW, &currentH);
        if (static_cast<int>(currentW) == width &&
            static_cast<int>(currentH) == height)
        {
            return true;
        }
        SDL_DestroyTexture(canvas);
        canvas = nullptr;
    }

    canvas =
        SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888,
                          SDL_TEXTUREACCESS_TARGET, width, height);
    if (!canvas)
    {
        SDL_Log("SDL_CreateTexture() for window canvas failed: %s",
                SDL_GetError());
        return false;
    }

    SDL_SetTextureScaleMode(canvas, SDL_SCALEMODE_NEAREST);
    SDL_SetTextureBlendMode(canvas, SDL_BLENDMODE_BLEND);
    logWindowScaleDiagnostics(window, canvas, state->pixelScale);
    return true;
}

void Window::setFocusedComponent(Component *component)
{
    if (component != nullptr && !component->acceptsKeyboardFocus())
    {
        component = nullptr;
    }

    if (focusedComponent == component)
    {
        return;
    }

    if (focusedComponent != nullptr)
    {
        focusedComponent->focusLost();
    }

    focusedComponent = component;

    if (focusedComponent != nullptr)
    {
        focusedComponent->focusGained();
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
        const float displayScale =
            getEffectiveWindowDisplayScale(window, nullptr);
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
    logWindowScaleDiagnostics(window, canvas, state->pixelScale);
}

void Window::handleResize()
{
    if (!window)
    {
        return;
    }

    hideTooltip();

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
    setFontDisplayScale(getEffectiveWindowDisplayScale(window, canvas));
    if (onResize)
    {
        onResize();
    }
}

void Window::refreshForScaleOrResize()
{
    hideTooltip();
    resizeCanvasIfNeeded();
    setFontDisplayScale(getEffectiveWindowDisplayScale(window, canvas));
    logWindowScaleDiagnostics(window, canvas, state->pixelScale);
    if (onResize)
    {
        onResize();
    }
}

void Window::close()
{
    hideTooltip();
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

void Window::updateHoverFromCurrentMousePosition()
{
    if (!window)
    {
        return;
    }

    float mouseX = 0.0f;
    float mouseY = 0.0f;
    SDL_GetMouseState(&mouseX, &mouseY);

    SDL_Event motion{};
    motion.type = SDL_EVENT_MOUSE_MOTION;
    motion.motion.windowID = windowId;
    motion.motion.x = mouseX;
    motion.motion.y = mouseY;

    const auto mouseEvent = makeMouseEvent(motion);
    updateComponentUnderMouse(mouseEvent.mouseXi, mouseEvent.mouseYi);
}

void Window::updateTooltip()
{
    if (tooltipController)
    {
        tooltipController->update();
    }
}

void Window::hideTooltip()
{
    if (tooltipController)
    {
        tooltipController->hide();
    }
}

SDL_Rect Window::mapCanvasRectToScreenRect(const SDL_Rect &rect) const
{
    if (!window)
    {
        return SDL_Rect{0, 0, 0, 0};
    }

    int windowX = 0;
    int windowY = 0;
    SDL_GetWindowPosition(window, &windowX, &windowY);

    int logicalW = 0;
    int logicalH = 0;
    SDL_GetWindowSize(window, &logicalW, &logicalH);

    float canvasW = 0.0f;
    float canvasH = 0.0f;
    if (canvas)
    {
        SDL_GetTextureSize(canvas, &canvasW, &canvasH);
    }

    return cupuacu::gui::mapCanvasRectToScreenRect(
        rect, SDL_Rect{windowX, windowY, logicalW, logicalH},
        SDL_FPoint{canvasW, canvasH});
}

bool Window::handleEvent(const SDL_Event &event)
{
    if (!isEventForWindow(event))
    {
        return false;
    }

    if (event.type == SDL_EVENT_KEY_DOWN)
    {
        const bool isMainDocumentWindow =
            state != nullptr && state->mainDocumentSessionWindow != nullptr &&
            state->mainDocumentSessionWindow->getWindow() == this;

        const bool handledByFocusedComponent =
            focusedComponent != nullptr && focusedComponent->keyDown(event.key);
        if (handledByFocusedComponent)
        {
            return true;
        }

        if ((event.key.scancode == SDL_SCANCODE_RETURN ||
             event.key.scancode == SDL_SCANCODE_RETURN2 ||
             event.key.scancode == SDL_SCANCODE_KP_ENTER) &&
            defaultAction)
        {
            defaultAction();
            return true;
        }

        if (event.key.scancode == SDL_SCANCODE_ESCAPE && !isMainDocumentWindow)
        {
            if (cancelAction)
            {
                cancelAction();
                return true;
            }

            requestClose();
            if (closeRequested)
            {
                closeRequested = false;
                if (onClose)
                {
                    onClose();
                }
                close();
            }
            return true;
        }

        return onUnhandledKeyDown ? onUnhandledKeyDown(event.key) : false;
    }

    if (event.type == SDL_EVENT_TEXT_INPUT)
    {
        return focusedComponent != nullptr &&
               focusedComponent->textInput(event.text.text);
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

    if (closeRequested)
    {
        closeRequested = false;
        if (onClose)
        {
            onClose();
        }
        close();
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

    if (mouseEvent.type == DOWN && rootComponent != nullptr)
    {
        DropdownMenu *clickedDropdown = findAncestorDropdown(componentUnderMouse);
        if (collapseExpandedDropdowns(this, state, clickedDropdown))
        {
            updateComponentUnderMouse(mouseEvent.mouseXi, mouseEvent.mouseYi);
        }
    }

    if (mouseEvent.type == DOWN && focusedComponent != nullptr &&
        (componentUnderMouse == nullptr ||
         !Component::isComponentOrChildOf(componentUnderMouse, focusedComponent)))
    {
        setFocusedComponent(nullptr);
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

    if (closeRequested)
    {
        closeRequested = false;
        if (onClose)
        {
            onClose();
        }
        close();
    }

    return true;
}

void Window::renderFrame()
{
    if (!renderer || !rootComponent || !canvas)
    {
        return;
    }

    setFontDisplayScale(getEffectiveWindowDisplayScale(window, canvas));

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
    if (transparentWindow)
    {
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
        SDL_RenderClear(renderer);
    }
    rootComponent->draw(renderer);
    SDL_SetRenderTarget(renderer, nullptr);
    if (transparentWindow)
    {
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
        SDL_RenderClear(renderer);
    }
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

    setFontDisplayScale(getEffectiveWindowDisplayScale(window, canvas));

    // Overlay must repaint whenever anything below changes so popups stay on
    // top even when underlying content (e.g. waveforms) is animating.
    if (overlayLayer)
    {
        overlayLayer->setDirty();
    }

    SDL_SetRenderTarget(renderer, canvas);
    if (transparentWindow)
    {
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
        SDL_RenderClear(renderer);
    }
    rootComponent->draw(renderer);
    SDL_SetRenderTarget(renderer, nullptr);
    if (transparentWindow)
    {
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
        SDL_RenderClear(renderer);
    }
    SDL_RenderTexture(renderer, canvas, nullptr, nullptr);
    SDL_RenderPresent(renderer);
    dirtyRects.clear();
}
