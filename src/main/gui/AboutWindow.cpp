#include "AboutWindow.hpp"

#include "../BuildInfo.hpp"
#include "../ResourceUtil.hpp"
#include "../State.hpp"
#include "Colors.hpp"
#include "Component.hpp"
#include "Label.hpp"
#include "OpaqueRect.hpp"
#include "SecondaryWindowLifecycle.hpp"
#include "SelectableTextView.hpp"
#include "TextButton.hpp"
#include "UiScale.hpp"
#include "Window.hpp"
#include "text.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace
{
    constexpr int kWindowWidth = 760;
    constexpr int kWindowHeight = 600;
    constexpr SDL_Color kActiveSectionColor{74, 110, 170, 255};

    class LogoWatermark final : public cupuacu::gui::Component
    {
    public:
        explicit LogoWatermark(cupuacu::State *state)
            : Component(state, "AboutLogoWatermark"),
              logoData(cupuacu::get_resource_data("cupuacu-logo1.bmp"))
        {
            setInterceptMouseEnabled(false);
        }

        ~LogoWatermark() override
        {
            if (scaledTexture)
            {
                SDL_DestroyTexture(scaledTexture);
            }
            if (texture)
            {
                SDL_DestroyTexture(texture);
            }
        }

        bool isOpaque() const override
        {
            return false;
        }

        void onDraw(SDL_Renderer *renderer,
                    const SDL_Rect &invalidLocalRect) override
        {
            if (!texture && !logoData.empty())
            {
                SDL_IOStream *io = SDL_IOFromConstMem(
                    logoData.data(), static_cast<int>(logoData.size()));
                SDL_Surface *surface = io ? SDL_LoadBMP_IO(io, true) : nullptr;
                if (surface)
                {
                    sourceWidth = surface->w;
                    sourceHeight = surface->h;
                    texture = SDL_CreateTextureFromSurface(renderer, surface);
                    SDL_DestroySurface(surface);
                    if (texture)
                    {
                        SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
                        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
                    }
                }
            }
            if (!texture || sourceWidth <= 0 || sourceHeight <= 0)
            {
                return;
            }

            const float availableWidth = getLocalBoundsF().w * 0.72f;
            const float availableHeight = getLocalBoundsF().h * 0.72f;
            const float scale = std::max(
                1.0f, std::floor(std::min(availableWidth / sourceWidth,
                                          availableHeight / sourceHeight)));
            const int width = static_cast<int>(std::round(sourceWidth * scale));
            const int height =
                static_cast<int>(std::round(sourceHeight * scale));
            ensureScaledTexture(renderer, width, height);
            if (!scaledTexture)
            {
                return;
            }

            const SDL_Rect destination{
                static_cast<int>(
                    std::round((getLocalBoundsF().w - width) * 0.5f)),
                static_cast<int>(
                    std::round((getLocalBoundsF().h - height) * 0.5f)),
                width, height};
            SDL_Rect visibleDestination{};
            if (!SDL_GetRectIntersection(&destination, &invalidLocalRect,
                                         &visibleDestination))
            {
                return;
            }

            const SDL_FRect source{
                static_cast<float>(visibleDestination.x - destination.x),
                static_cast<float>(visibleDestination.y - destination.y),
                static_cast<float>(visibleDestination.w),
                static_cast<float>(visibleDestination.h)};
            const SDL_FRect clippedDestination{
                static_cast<float>(visibleDestination.x - invalidLocalRect.x),
                static_cast<float>(visibleDestination.y - invalidLocalRect.y),
                static_cast<float>(visibleDestination.w),
                static_cast<float>(visibleDestination.h)};
            SDL_RenderTexture(renderer, scaledTexture, &source,
                              &clippedDestination);
        }

    private:
        std::string logoData;
        SDL_Texture *texture = nullptr;
        SDL_Texture *scaledTexture = nullptr;
        int sourceWidth = 0;
        int sourceHeight = 0;
        int scaledWidth = 0;
        int scaledHeight = 0;

        void ensureScaledTexture(SDL_Renderer *renderer, const int width,
                                 const int height)
        {
            if (scaledTexture && width == scaledWidth && height == scaledHeight)
            {
                return;
            }

            if (scaledTexture)
            {
                SDL_DestroyTexture(scaledTexture);
                scaledTexture = nullptr;
            }
            scaledWidth = 0;
            scaledHeight = 0;
            if (width <= 0 || height <= 0)
            {
                return;
            }

            scaledTexture =
                SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888,
                                  SDL_TEXTUREACCESS_TARGET, width, height);
            if (!scaledTexture)
            {
                return;
            }
            SDL_SetTextureScaleMode(scaledTexture, SDL_SCALEMODE_NEAREST);
            SDL_SetTextureBlendMode(scaledTexture, SDL_BLENDMODE_BLEND);
            SDL_SetTextureAlphaMod(scaledTexture, 76);

            SDL_Texture *previousTarget = SDL_GetRenderTarget(renderer);
            SDL_Rect previousViewport{};
            SDL_GetRenderViewport(renderer, &previousViewport);
            const bool clipWasEnabled = SDL_RenderClipEnabled(renderer);
            SDL_Rect previousClip{};
            SDL_GetRenderClipRect(renderer, &previousClip);
            SDL_BlendMode previousDrawBlendMode = SDL_BLENDMODE_BLEND;
            SDL_GetRenderDrawBlendMode(renderer, &previousDrawBlendMode);

            SDL_SetRenderTarget(renderer, scaledTexture);
            SDL_SetRenderViewport(renderer, nullptr);
            SDL_SetRenderClipRect(renderer, nullptr);
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
            SDL_RenderClear(renderer);
            const SDL_FRect destination{0.0f, 0.0f, static_cast<float>(width),
                                        static_cast<float>(height)};
            SDL_RenderTexture(renderer, texture, nullptr, &destination);

            SDL_SetRenderTarget(renderer, previousTarget);
            SDL_SetRenderViewport(renderer, &previousViewport);
            SDL_SetRenderClipRect(renderer,
                                  clipWasEnabled ? &previousClip : nullptr);
            SDL_SetRenderDrawBlendMode(renderer, previousDrawBlendMode);

            scaledWidth = width;
            scaledHeight = height;
        }
    };

    bool primaryModifierHeld(const SDL_KeyboardEvent &event)
    {
#if __APPLE__
        return (event.mod & SDL_KMOD_GUI) != 0;
#else
        return (event.mod & SDL_KMOD_CTRL) != 0;
#endif
    }

    void copyText(const std::string &text)
    {
        (void)SDL_SetClipboardText(text.c_str());
    }
} // namespace

using namespace cupuacu::gui;

AboutWindow::AboutWindow(State *stateToUse) : state(stateToUse)
{
    if (!state)
    {
        return;
    }

    window = std::make_unique<Window>(
        state, "About Cupuacu", kWindowWidth, kWindowHeight,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window->isOpen())
    {
        return;
    }
    SDL_SetWindowMinimumSize(window->getSdlWindow(), 640, 480);
    attachSecondaryWindow(state, window.get(), false);

    auto root = std::make_unique<Component>(state, "AboutRoot");
    background = root->emplaceChild<OpaqueRect>(state, Colors::background);
    watermark = root->emplaceChild<LogoWatermark>(state);
    title = root->emplaceChild<Label>(state, "Cupuacu");
    tagline = root->emplaceChild<Label>(
        state,
        "A minimalist cross-platform audio editor inspired by Cool Edit");
    version = root->emplaceChild<Label>(
        state, "Version " + cupuacu::build::applicationVersion());
    aboutButton = root->emplaceChild<TextButton>(state, "About & Build");
    creditsButton = root->emplaceChild<TextButton>(state, "Credits & Licenses");
    textView = root->emplaceChild<SelectableTextView>(state);
    copyVersionButton = root->emplaceChild<TextButton>(state, "Copy version");
    copyDetailsButton =
        root->emplaceChild<TextButton>(state, "Copy diagnostics");
    projectWebsiteButton =
        root->emplaceChild<TextButton>(state, "Project website");

    const int menuFontSize = state ? static_cast<int>(state->menuFontSize) : 30;
    const int bodyFontSize = std::max(18, menuFontSize - 6);

    title->setCenterHorizontally(true);
    title->setFontSize(menuFontSize + 8);
    tagline->setCenterHorizontally(true);
    tagline->setFontSize(menuFontSize - 4);
    version->setCenterHorizontally(true);
    version->setFontSize(menuFontSize - 4);
    textView->setFontSize(bodyFontSize);

    for (auto *button : {aboutButton, creditsButton, copyVersionButton,
                         copyDetailsButton, projectWebsiteButton})
    {
        button->setTriggerOnMouseUp(true);
        button->setFontSize(bodyFontSize);
    }

    aboutButton->setOnPress(
        [this]()
        {
            selectSection(AboutSection::AboutAndBuild);
        });
    creditsButton->setOnPress(
        [this]()
        {
            selectSection(AboutSection::CreditsAndLicenses);
        });
    copyVersionButton->setOnPress(
        []()
        {
            copyText(cupuacu::build::applicationVersion());
        });
    copyDetailsButton->setOnPress(
        [this]()
        {
            if (selectedSection == AboutSection::CreditsAndLicenses)
            {
                copyText(cupuacu::build::creditsText());
            }
            else
            {
                copyText(cupuacu::build::diagnosticReport(rendererName()));
            }
        });
    projectWebsiteButton->setOnPress(
        []()
        {
            (void)SDL_OpenURL("https://github.com/izzyreal/cupuacu");
        });

    window->setOnResize(
        [this]()
        {
            layoutComponents();
            renderOnce();
        });
    window->setCancelAction(
        [this]()
        {
            if (window)
            {
                window->requestClose();
            }
        });
    window->setOnUnhandledKeyDown(
        [this](const SDL_KeyboardEvent &event)
        {
            if (primaryModifierHeld(event) && event.scancode == SDL_SCANCODE_W)
            {
                window->requestClose();
                return true;
            }
            return false;
        });
    window->setOnClose(
        [this]()
        {
            detachSecondaryWindow(state, window.get());
        });

    window->setRootComponent(std::move(root));
    selectSection(AboutSection::AboutAndBuild);
    layoutComponents();
    window->setFocusedComponent(textView);
    renderOnce();
    raise();
}

AboutWindow::~AboutWindow()
{
    detachSecondaryWindow(state, window.get());
}

bool AboutWindow::isOpen() const
{
    return window && window->isOpen();
}

void AboutWindow::raise() const
{
    raiseSecondaryWindow(window.get());
}

void AboutWindow::selectSection(const AboutSection section)
{
    selectedSection = section;
    aboutButton->setForcedFillColor(
        section == AboutSection::AboutAndBuild
            ? std::optional<SDL_Color>{kActiveSectionColor}
            : std::nullopt);
    creditsButton->setForcedFillColor(
        section == AboutSection::CreditsAndLicenses
            ? std::optional<SDL_Color>{kActiveSectionColor}
            : std::nullopt);
    copyDetailsButton->setText(section == AboutSection::CreditsAndLicenses
                                   ? "Copy credits"
                                   : "Copy diagnostics");
    updateContent();
    layoutComponents();
    renderOnce();
}

std::string AboutWindow::rendererName() const
{
    if (!window || !window->getRenderer())
    {
        return {};
    }
    const char *name = SDL_GetRendererName(window->getRenderer());
    return name ? std::string(name) : std::string{};
}

void AboutWindow::layoutComponents() const
{
    if (!window || !window->getRootComponent())
    {
        return;
    }

    float canvasWidth = static_cast<float>(kWindowWidth);
    float canvasHeight = static_cast<float>(kWindowHeight);
    if (window->getCanvas())
    {
        SDL_GetTextureSize(window->getCanvas(), &canvasWidth, &canvasHeight);
    }

    const int width = static_cast<int>(canvasWidth);
    const int height = static_cast<int>(canvasHeight);
    const int padding = scaleUi(state, 14.0f);
    const int gap = scaleUi(state, 8.0f);
    const int titleHeight = scaleUi(state, 52.0f);
    const int lineHeight = scaleUi(state, 34.0f);
    const int tabHeight = scaleUi(state, 46.0f);
    const int footerHeight = scaleUi(state, 46.0f);
    const int bodyFontSize =
        state ? std::max(18, static_cast<int>(state->menuFontSize) - 6) : 24;
    const uint8_t renderedBodyFontSize =
        scaleFontPointSize(state, bodyFontSize);
    const int buttonPadding = scaleUi(state, 24.0f);
    const int copyVersionWidth =
        measureText("Copy version", renderedBodyFontSize).first + buttonPadding;
    const int copyDetailsWidth =
        measureText(selectedSection == AboutSection::CreditsAndLicenses
                        ? "Copy credits"
                        : "Copy diagnostics",
                    renderedBodyFontSize)
            .first +
        buttonPadding;
    const int websiteWidth =
        measureText("Project website", renderedBodyFontSize).first +
        buttonPadding;

    window->getRootComponent()->setSize(width, height);
    background->setBounds(0, 0, width, height);
    watermark->setBounds(0, 0, width, height);

    int y = padding;
    title->setBounds(padding, y, std::max(0, width - padding * 2), titleHeight);
    y += titleHeight;
    tagline->setBounds(padding, y, std::max(0, width - padding * 2),
                       lineHeight);
    y += lineHeight;
    version->setBounds(padding, y, std::max(0, width - padding * 2),
                       lineHeight);
    y += lineHeight + gap;

    const int tabWidth =
        std::max(scaleUi(state, 170.0f), (width - padding * 2 - gap) / 2);
    aboutButton->setBounds(padding, y, tabWidth, tabHeight);
    creditsButton->setBounds(padding + tabWidth + gap, y,
                             std::max(0, width - padding * 2 - tabWidth - gap),
                             tabHeight);
    y += tabHeight + gap;

    const int footerY = std::max(y, height - padding - footerHeight);
    textView->setBounds(padding, y, std::max(0, width - padding * 2),
                        std::max(0, footerY - y - gap));
    copyVersionButton->setBounds(padding, footerY, copyVersionWidth,
                                 footerHeight);
    copyDetailsButton->setBounds(padding + copyVersionWidth + gap, footerY,
                                 copyDetailsWidth, footerHeight);
    projectWebsiteButton->setBounds(
        std::max(padding, width - padding - websiteWidth), footerY,
        websiteWidth, footerHeight);
}

void AboutWindow::updateContent()
{
    if (!textView)
    {
        return;
    }
    if (selectedSection == AboutSection::CreditsAndLicenses)
    {
        textView->setText(cupuacu::build::creditsText());
    }
    else
    {
        textView->setText(cupuacu::build::aboutText(rendererName()));
    }
}

void AboutWindow::renderOnce() const
{
    if (window)
    {
        window->renderFrame();
    }
}

void cupuacu::gui::showAboutWindow(State *state)
{
    if (!state)
    {
        return;
    }
    if (!state->aboutWindow || !state->aboutWindow->isOpen())
    {
        state->aboutWindow.reset(new AboutWindow(state));
    }
    if (state->aboutWindow)
    {
        state->aboutWindow->raise();
    }
}
