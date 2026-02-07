#pragma once

#include <SDL3/SDL.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "Component.h"
#include "MouseEvent.h"

namespace cupuacu
{
    struct State;
}

namespace cupuacu::gui
{
    class MenuBar;

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

        void setOnResize(std::function<void()> callback)
        {
            onResize = std::move(callback);
        }
        void setOnClose(std::function<void()> callback)
        {
            onClose = std::move(callback);
        }

        bool handleEvent(const SDL_Event &event);
        void renderFrame();
        void renderFrameIfDirty();
        void refreshForScaleOrResize();

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

        std::unique_ptr<Component> rootComponent;
        std::vector<SDL_Rect> dirtyRects;

        Component *capturingComponent = nullptr;
        Component *componentUnderMouse = nullptr;
        MenuBar *menuBar = nullptr;
        Component *contentLayer = nullptr;
        Component *overlayLayer = nullptr;

        std::function<void()> onResize;
        std::function<void()> onClose;

        void close();
        bool isEventForWindow(const SDL_Event &event) const;
        void handleResize();
        void resizeCanvasIfNeeded();
        SDL_Point computeRequiredCanvasDimensions() const;
    };
} // namespace cupuacu::gui
