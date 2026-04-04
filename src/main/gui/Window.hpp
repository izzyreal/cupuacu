#pragma once

#include <SDL3/SDL.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "Component.hpp"
#include "MouseEvent.hpp"

namespace cupuacu
{
    struct State;
}

namespace cupuacu::gui
{
    class MenuBar;
    class TooltipController;

    class Window
    {
    public:
        Window(State *stateToUse, const std::string &title, int width,
               int height, Uint32 flags);
        ~Window();

        bool isOpen() const
        {
            return window != nullptr;
        }
        bool hasFocus() const;
        SDL_WindowID getId() const
        {
            return windowId;
        }
        SDL_Window *getSdlWindow() const
        {
            return window;
        }
        SDL_Renderer *getRenderer() const
        {
            return renderer;
        }
        SDL_Texture *getCanvas() const
        {
            return canvas;
        }
        bool setCanvasSize(int width, int height);

        void setRootComponent(std::unique_ptr<Component> rootToUse);
        Component *getRootComponent() const
        {
            return rootComponent.get();
        }

        void setMenuBar(MenuBar *menuBarToUse)
        {
            menuBar = menuBarToUse;
        }
        MenuBar *getMenuBar() const
        {
            return menuBar;
        }

        void setOverlayLayer(Component *overlayLayerToUse)
        {
            overlayLayer = overlayLayerToUse;
        }
        Component *getOverlayLayer() const
        {
            return overlayLayer;
        }
        void setContentLayer(Component *contentLayerToUse)
        {
            contentLayer = contentLayerToUse;
        }
        Component *getContentLayer() const
        {
            return contentLayer;
        }

        std::vector<SDL_Rect> &getDirtyRects()
        {
            return dirtyRects;
        }

        Component *getCapturingComponent() const
        {
            return capturingComponent;
        }
        void setCapturingComponent(Component *component)
        {
            capturingComponent = component;
        }

        Component *getComponentUnderMouse() const
        {
            return componentUnderMouse;
        }
        void setComponentUnderMouse(Component *component)
        {
            componentUnderMouse = component;
        }
        Component *getFocusedComponent() const
        {
            return focusedComponent;
        }
        bool hasFocusedComponent() const
        {
            return focusedComponent != nullptr;
        }
        void setFocusedComponent(Component *component);

        void setOnResize(std::function<void()> callback)
        {
            onResize = std::move(callback);
        }
        void setOnUnhandledKeyDown(
            std::function<bool(const SDL_KeyboardEvent &)> callback)
        {
            onUnhandledKeyDown = std::move(callback);
        }
        void setDefaultAction(std::function<void()> callback)
        {
            defaultAction = std::move(callback);
        }
        void setCancelAction(std::function<void()> callback)
        {
            cancelAction = std::move(callback);
        }
        void setOnClose(std::function<void()> callback)
        {
            onClose = std::move(callback);
        }

        bool handleEvent(const SDL_Event &event);
        bool handleMouseEvent(const MouseEvent &event);
        void requestClose()
        {
            closeRequested = true;
        }
        bool isDispatching() const
        {
            return dispatchDepth > 0;
        }
        void updateHoverFromCurrentMousePosition();
        void renderFrame();
        void renderFrameIfDirty();
        void refreshForScaleOrResize();
        void updateTooltip();
        void hideTooltip();
        SDL_Rect mapCanvasRectToScreenRect(const SDL_Rect &rect) const;

        MouseEvent makeMouseEvent(const SDL_Event &event) const;
        void updateComponentUnderMouse(const int32_t mouseX,
                                       const int32_t mouseY);

    private:
        State *state = nullptr;
        SDL_Window *window = nullptr;
        SDL_Renderer *renderer = nullptr;
        SDL_Texture *canvas = nullptr;
        SDL_WindowID windowId = 0;
        bool wasMaximized = false;
        bool transparentWindow = false;

        std::unique_ptr<Component> rootComponent;
        std::vector<SDL_Rect> dirtyRects;

        Component *capturingComponent = nullptr;
        Component *componentUnderMouse = nullptr;
        Component *focusedComponent = nullptr;
        MenuBar *menuBar = nullptr;
        Component *contentLayer = nullptr;
        Component *overlayLayer = nullptr;
        std::unique_ptr<TooltipController> tooltipController;

        std::function<void()> onResize;
        std::function<bool(const SDL_KeyboardEvent &)> onUnhandledKeyDown;
        std::function<void()> defaultAction;
        std::function<void()> cancelAction;
        std::function<void()> onClose;
        bool closeRequested = false;
        int dispatchDepth = 0;
        bool suppressMouseUpAfterDropdownDismiss = false;

        void close();
        bool isEventForWindow(const SDL_Event &event) const;
        void handleResize();
        void resizeCanvasIfNeeded();
        SDL_Point computeRequiredCanvasDimensions() const;
    };
} // namespace cupuacu::gui
