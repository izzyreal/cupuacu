#include "OptionsWindow.hpp"

#include "Colors.hpp"
#include "SecondaryWindowLifecycle.hpp"
#include "UiScale.hpp"
#include "text.hpp"

#include <algorithm>
#include <cmath>

using namespace cupuacu::gui;

namespace
{
    constexpr int kWindowWidth = 700;
    constexpr int kWindowHeight = 420;
    constexpr SDL_Color kSidebarActiveColor{74, 110, 170, 255};

    constexpr Uint32 getHighDensityWindowFlag()
    {
        return SDL_WINDOW_HIGH_PIXEL_DENSITY;
    }
} // namespace

OptionsWindow::OptionsWindow(State *stateToUse,
                             const OptionsSection initialSection)
    : state(stateToUse), selectedSection(initialSection)
{
    if (!state)
    {
        return;
    }

    window = std::make_unique<Window>(
        state, "Options", kWindowWidth, kWindowHeight,
        SDL_WINDOW_RESIZABLE | getHighDensityWindowFlag());
    if (!window->isOpen())
    {
        return;
    }

    attachSecondaryWindow(state, window.get(), false);

    auto rootComponent = std::make_unique<Component>(state, "OptionsRoot");
    rootComponent->setVisible(true);

    background = rootComponent->emplaceChild<OpaqueRect>(state, Colors::background);
    sidebarBackground =
        rootComponent->emplaceChild<OpaqueRect>(state, SDL_Color{34, 34, 34, 255});
    audioButton = rootComponent->emplaceChild<TextButton>(state, "Audio");
    displayButton = rootComponent->emplaceChild<TextButton>(state, "Display");
    audioPane = rootComponent->emplaceChild<DevicePropertiesPane>(state);
    displayPane = rootComponent->emplaceChild<DisplaySettingsPane>(state);

    audioButton->setTriggerOnMouseUp(true);
    displayButton->setTriggerOnMouseUp(true);
    audioButton->setOnPress([this]() { selectSection(OptionsSection::Audio); });
    displayButton->setOnPress(
        [this]() { selectSection(OptionsSection::Display); });

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
    window->setOnClose(
        [this]()
        {
            detachSecondaryWindow(state, window.get());
        });

    window->setRootComponent(std::move(rootComponent));
    selectSection(initialSection);
    layoutComponents();
    renderOnce();
    raise();
}

OptionsWindow::~OptionsWindow()
{
    detachSecondaryWindow(state, window.get());
}

void OptionsWindow::raise() const
{
    raiseSecondaryWindow(window.get());
}

void OptionsWindow::selectSection(const OptionsSection section)
{
    selectedSection = section;
    if (state)
    {
        state->lastSelectedOptionsSection = section;
    }
    if (audioPane)
    {
        audioPane->setVisible(section == OptionsSection::Audio);
    }
    if (displayPane)
    {
        displayPane->setVisible(section == OptionsSection::Display);
    }
    syncSidebarButtons();
    renderOnce();
}

void OptionsWindow::layoutComponents() const
{
    if (!window || !window->getRootComponent())
    {
        return;
    }

    float canvasW = static_cast<float>(kWindowWidth);
    float canvasH = static_cast<float>(kWindowHeight);
    if (window->getCanvas())
    {
        SDL_GetTextureSize(window->getCanvas(), &canvasW, &canvasH);
    }

    const int canvasWi = static_cast<int>(canvasW);
    const int canvasHi = static_cast<int>(canvasH);
    const int padding = scaleUi(state, 12.0f);
    const int gap = std::max(6, scaleUi(state, 8.0f));
    const uint8_t buttonFontPointSize =
        scaleFontPointSize(state, state ? state->menuFontSize : 24);
    const auto [audioTextW, audioTextH] =
        measureText("Audio", buttonFontPointSize);
    const auto [displayTextW, displayTextH] =
        measureText("Display", buttonFontPointSize);
    const int buttonTextWidth = std::max(
        std::max(1, static_cast<int>(std::ceil(audioTextW))),
        std::max(1, static_cast<int>(std::ceil(displayTextW))));
    const int buttonTextHeight = std::max(
        std::max(1, static_cast<int>(std::ceil(audioTextH))),
        std::max(1, static_cast<int>(std::ceil(displayTextH))));
    const int buttonHeight =
        std::max(scaleUi(state, 36.0f), buttonTextHeight + padding * 2);
    const int buttonWidth =
        std::max(scaleUi(state, 132.0f), buttonTextWidth + padding * 2);
    const int sidebarWidth =
        std::max(scaleUi(state, 176.0f), buttonWidth + padding * 2);

    window->getRootComponent()->setSize(canvasWi, canvasHi);
    background->setBounds(0, 0, canvasWi, canvasHi);
    sidebarBackground->setBounds(0, 0, sidebarWidth, canvasHi);
    audioButton->setFontSize(state ? state->menuFontSize : 24);
    displayButton->setFontSize(state ? state->menuFontSize : 24);
    audioButton->setBounds(padding, padding, buttonWidth, buttonHeight);
    displayButton->setBounds(padding, padding + buttonHeight + gap,
                             buttonWidth, buttonHeight);

    const SDL_Rect contentBounds{
        sidebarWidth + padding, padding,
        std::max(0, canvasWi - sidebarWidth - padding * 2),
        std::max(0, canvasHi - padding * 2)};
    audioPane->setBounds(contentBounds);
    displayPane->setBounds(contentBounds);
}

void OptionsWindow::syncSidebarButtons() const
{
    if (!audioButton || !displayButton)
    {
        return;
    }

    audioButton->setForcedFillColor(
        selectedSection == OptionsSection::Audio
            ? std::optional<SDL_Color>{kSidebarActiveColor}
            : std::nullopt);
    displayButton->setForcedFillColor(
        selectedSection == OptionsSection::Display
            ? std::optional<SDL_Color>{kSidebarActiveColor}
            : std::nullopt);
}

void OptionsWindow::renderOnce() const
{
    if (window)
    {
        window->renderFrame();
    }
}

void cupuacu::gui::showOptionsWindow(
    State *state, const std::optional<OptionsSection> section)
{
    if (!state)
    {
        return;
    }

    if (!state->optionsWindow || !state->optionsWindow->isOpen())
    {
        state->optionsWindow.reset(new OptionsWindow(
            state, section.value_or(state->lastSelectedOptionsSection)));
    }
    else if (section.has_value())
    {
        state->optionsWindow->selectSection(*section);
    }

    if (state->optionsWindow)
    {
        state->optionsWindow->raise();
    }
}
