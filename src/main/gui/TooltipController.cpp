#include "TooltipController.hpp"

#include "../State.hpp"

#include "Colors.hpp"
#include "Component.hpp"
#include "MenuBar.hpp"
#include "RoundedRect.hpp"
#include "TooltipPlanning.hpp"
#include "UiScale.hpp"
#include "Window.hpp"
#include "WindowResizePlanning.hpp"
#include "text.hpp"

#include <algorithm>
#include <cmath>

namespace
{
    constexpr Uint64 kTooltipHoverDelayMs = 500;
    constexpr SDL_Color kTooltipFill{50, 50, 50, 230};
    constexpr SDL_Color kTooltipOutline{180, 180, 180, 220};

    constexpr Uint32 getTooltipHighDensityWindowFlag()
    {
#if defined(__linux__)
        return 0;
#else
        return SDL_WINDOW_HIGH_PIXEL_DENSITY;
#endif
    }

    bool rectsEqual(const SDL_Rect &a, const SDL_Rect &b)
    {
        return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
    }
}

namespace cupuacu::gui
{
    class TooltipPopupWindow
    {
    public:
        explicit TooltipPopupWindow(State *stateToUse) : state(stateToUse) {}

        ~TooltipPopupWindow()
        {
            destroy();
        }

        void show(SDL_Window *parentWindow, const SDL_Rect &anchorBounds,
                  const std::string &text)
        {
            if (!parentWindow || text.empty())
            {
                hide();
                return;
            }

            const float displayScale =
                std::max(1.0f, SDL_GetWindowDisplayScale(parentWindow));
            const float previousFontDisplayScale = getFontDisplayScale();
            setFontDisplayScale(displayScale);

            const uint8_t fontPointSize = scaleFontPointSize(
                state, std::max(1, static_cast<int>(state->menuFontSize) - 6));
            const int paddingCanvas = scaleUi(state, 10.0f);
            const int gapCanvas = scaleUi(state, 8.0f);
            const auto [textWidthPx, textHeightPx] =
                measureText(text, fontPointSize);
            const SDL_DisplayID displayId = SDL_GetDisplayForWindow(parentWindow);
            SDL_Rect displayBounds{0, 0, 0, 0};
            if (!displayId ||
                !SDL_GetDisplayUsableBounds(displayId, &displayBounds))
            {
                if (!SDL_GetDisplayBounds(displayId, &displayBounds))
                {
                    displayBounds = SDL_Rect{0, 0, 1920, 1080};
                }
            }

            const auto geometry = planTooltipPopupGeometry(
                textWidthPx, textHeightPx, displayScale, paddingCanvas,
                gapCanvas, state ? state->pixelScale : 1, displayBounds.w,
                displayBounds.h);
            if (!geometry.valid)
            {
                setFontDisplayScale(previousFontDisplayScale);
                hide();
                return;
            }

            int parentX = 0;
            int parentY = 0;
            SDL_GetWindowPosition(parentWindow, &parentX, &parentY);
            int parentW = 0;
            int parentH = 0;
            SDL_GetWindowSize(parentWindow, &parentW, &parentH);
            const SDL_Rect parentBounds{parentX, parentY, parentW, parentH};

            const auto placement = planTooltipPopupPlacement(
                parentBounds, anchorBounds, displayBounds, geometry.logicalWidth,
                geometry.logicalHeight, geometry.gapLogical);
            if (!placement.valid)
            {
                setFontDisplayScale(previousFontDisplayScale);
                hide();
                return;
            }

            if (!ensureWindow(parentWindow, placement.offsetX, placement.offsetY,
                              geometry.logicalWidth, geometry.logicalHeight))
            {
                setFontDisplayScale(previousFontDisplayScale);
                hide();
                return;
            }

            if (!ensureCanvas(geometry.canvasWidth, geometry.canvasHeight))
            {
                setFontDisplayScale(previousFontDisplayScale);
                hide();
                return;
            }

            render(text, fontPointSize, geometry.renderPaddingPx);
            SDL_ShowWindow(window);
            visible = true;
            setFontDisplayScale(previousFontDisplayScale);
        }

        void hide()
        {
            destroy();
        }

        bool isVisible() const
        {
            return visible;
        }

    private:
        State *state = nullptr;
        SDL_Window *window = nullptr;
        SDL_Renderer *renderer = nullptr;
        SDL_Texture *canvas = nullptr;
        bool visible = false;

        void destroy()
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
            visible = false;
        }

        bool ensureCanvas(const int width, const int height)
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

            canvas = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888,
                                       SDL_TEXTUREACCESS_TARGET, width, height);
            if (!canvas)
            {
                SDL_Log("SDL_CreateTexture() for tooltip canvas failed: %s",
                        SDL_GetError());
                return false;
            }

            SDL_SetTextureScaleMode(canvas, SDL_SCALEMODE_NEAREST);
            SDL_SetTextureBlendMode(canvas, SDL_BLENDMODE_BLEND);
            return true;
        }

        bool ensureWindow(SDL_Window *parentWindow, const int offsetX,
                          const int offsetY, const int width,
                          const int height)
        {
            destroy();

            window = SDL_CreatePopupWindow(
                parentWindow, offsetX, offsetY, width, height,
                SDL_WINDOW_TOOLTIP | SDL_WINDOW_TRANSPARENT |
                    SDL_WINDOW_HIDDEN | getTooltipHighDensityWindowFlag());
            if (!window)
            {
                SDL_Log("SDL_CreatePopupWindow() failed: %s", SDL_GetError());
                return false;
            }

            renderer = SDL_CreateRenderer(window, nullptr);
            if (!renderer)
            {
                SDL_Log("SDL_CreateRenderer() for tooltip failed: %s",
                        SDL_GetError());
                destroy();
                return false;
            }

            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            return true;
        }

        void render(const std::string &text, const uint8_t fontPointSize,
                    const int renderPaddingPx)
        {
            if (!renderer || !canvas)
            {
                return;
            }

            float canvasWidth = 0.0f;
            float canvasHeight = 0.0f;
            SDL_GetTextureSize(canvas, &canvasWidth, &canvasHeight);

            SDL_SetRenderTarget(renderer, canvas);
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
            SDL_RenderClear(renderer);

            const SDL_FRect body{0.0f, 0.0f, canvasWidth, canvasHeight};
            const float radius = scaleUiF(state, 10.0f);
            drawRoundedRect(renderer, body, radius, kTooltipFill);
            drawRoundedRectOutline(renderer, body, radius, kTooltipOutline);

            const SDL_Rect clipRect{renderPaddingPx, renderPaddingPx,
                                    std::max(0, static_cast<int>(canvasWidth) -
                                                    renderPaddingPx * 2),
                                    std::max(0, static_cast<int>(canvasHeight) -
                                                    renderPaddingPx * 2)};
            SDL_SetRenderClipRect(renderer, &clipRect);
            renderText(renderer, text, fontPointSize,
                       SDL_FRect{static_cast<float>(renderPaddingPx),
                                 static_cast<float>(renderPaddingPx),
                                 static_cast<float>(clipRect.w),
                                 static_cast<float>(clipRect.h)},
                       false);
            SDL_SetRenderClipRect(renderer, nullptr);

            SDL_SetRenderTarget(renderer, nullptr);
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
            SDL_RenderClear(renderer);
            SDL_RenderTexture(renderer, canvas, nullptr, nullptr);
            SDL_RenderPresent(renderer);
        }
    };

    TooltipController::TooltipController(State *stateToUse, Window *windowToUse)
        : state(stateToUse), window(windowToUse),
          popupWindow(std::make_unique<TooltipPopupWindow>(stateToUse))
    {
    }

    TooltipController::~TooltipController() = default;

    Component *TooltipController::resolveTooltipSource(Component *component) const
    {
        while (component)
        {
            if (!component->getTooltipText().empty())
            {
                return component;
            }
            component = component->getParentComponent();
        }
        return nullptr;
    }

    void TooltipController::hide()
    {
        if (popupWindow)
        {
            popupWindow->hide();
        }
        shownSource = nullptr;
        shownText.clear();
        shownAnchor = SDL_Rect{0, 0, 0, 0};
        hoveredSource = nullptr;
        hoveredText.clear();
        hoveredAnchor = SDL_Rect{0, 0, 0, 0};
        hoveredSinceTicks = 0;
    }

    void TooltipController::update()
    {
        if (!window || !window->isOpen() || !window->getSdlWindow() ||
            !window->hasFocus() || !popupWindow)
        {
            hide();
            return;
        }

        if ((state && state->modalWindow && state->modalWindow != window) ||
            window->getCapturingComponent() != nullptr ||
            (window->getMenuBar() && window->getMenuBar()->hasMenuOpen()) ||
            SDL_GetMouseState(nullptr, nullptr) != 0)
        {
            hide();
            return;
        }

        auto *source = resolveTooltipSource(window->getComponentUnderMouse());
        if (!source)
        {
            hide();
            return;
        }

        const std::string text = source->getTooltipText();
        const SDL_Rect anchor = source->getTooltipAnchorBounds();
        const Uint64 now = SDL_GetTicks();

        if (source != hoveredSource || text != hoveredText ||
            !rectsEqual(anchor, hoveredAnchor))
        {
            hoveredSource = source;
            hoveredText = text;
            hoveredAnchor = anchor;
            hoveredSinceTicks = now;
            if (popupWindow->isVisible())
            {
                popupWindow->hide();
                shownSource = nullptr;
                shownText.clear();
                shownAnchor = SDL_Rect{0, 0, 0, 0};
            }
            return;
        }

        if (now - hoveredSinceTicks < kTooltipHoverDelayMs)
        {
            return;
        }

        const SDL_Rect screenAnchor = window->mapCanvasRectToScreenRect(anchor);
        if (screenAnchor.w <= 0 || screenAnchor.h <= 0)
        {
            hide();
            return;
        }

        if (source == shownSource && text == shownText &&
            rectsEqual(screenAnchor, shownAnchor))
        {
            return;
        }

        popupWindow->show(window->getSdlWindow(), screenAnchor, text);
        shownSource = source;
        shownText = text;
        shownAnchor = screenAnchor;
    }
} // namespace cupuacu::gui
