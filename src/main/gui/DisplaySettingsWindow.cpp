#include "DisplaySettingsWindow.hpp"

#include "Colors.hpp"
#include "ComponentLookup.hpp"
#include "Gui.hpp"
#include "VuMeterContainer.hpp"
#include "WaveformRefresh.hpp"
#include "persistence/DisplayPropertiesPersistence.hpp"
#include "text.hpp"

#include <algorithm>
#include <cmath>

using namespace cupuacu::gui;

namespace
{
    constexpr int kWindowWidth = 500;
    constexpr int kWindowHeight = 240;

    constexpr Uint32 getHighDensityWindowFlag()
    {
        return SDL_WINDOW_HIGH_PIXEL_DENSITY;
    }
} // namespace

DisplaySettingsWindow::DisplaySettingsWindow(State *stateToUse)
    : state(stateToUse)
{
    if (!state)
    {
        return;
    }

    window = std::make_unique<Window>(
        state, "Display", kWindowWidth, kWindowHeight,
        SDL_WINDOW_RESIZABLE | getHighDensityWindowFlag());
    if (!window->isOpen())
    {
        return;
    }

    state->windows.push_back(window.get());

    auto *mainWindow = state->mainDocumentSessionWindow
                           ? state->mainDocumentSessionWindow->getWindow()
                           : nullptr;
    if (mainWindow && mainWindow->getSdlWindow())
    {
        if (!SDL_SetWindowParent(window->getSdlWindow(),
                                 mainWindow->getSdlWindow()))
        {
            SDL_Log("DisplaySettingsWindow: SDL_SetWindowParent failed: %s",
                    SDL_GetError());
        }
    }

    auto rootComponent = std::make_unique<Component>(state, "DisplaySettingsRoot");
    rootComponent->setVisible(true);

    background = rootComponent->emplaceChild<OpaqueRect>(state, Colors::background);
    vuMeterScaleLabel = rootComponent->emplaceChild<Label>(state, "VU Meter Scale");
    vuMeterScaleDropdown = rootComponent->emplaceChild<DropdownMenu>(state);
    pixelScaleLabel = rootComponent->emplaceChild<Label>(state, "Pixel Scale");
    pixelScaleDropdown = rootComponent->emplaceChild<DropdownMenu>(state);

    const int labelFontSize = static_cast<int>(state->menuFontSize);
    vuMeterScaleLabel->setFontSize(labelFontSize);
    vuMeterScaleDropdown->setFontSize(labelFontSize);
    pixelScaleLabel->setFontSize(labelFontSize);
    pixelScaleDropdown->setFontSize(labelFontSize);
    vuMeterScaleDropdown->setExpanded(false);
    vuMeterScaleDropdown->setItems(vuMeterScaleOptionLabels());
    vuMeterScaleDropdown->setSelectedIndex(vuMeterScaleToIndex(state->vuMeterScale));
    pixelScaleDropdown->setExpanded(false);
    pixelScaleDropdown->setItems(displayPixelScaleOptionLabels());
    pixelScaleDropdown->setSelectedIndex(displayPixelScaleToIndex(state->pixelScale));
    vuMeterScaleDropdown->setOnSelectionChanged(
        [this](const int index)
        {
            if (!state)
            {
                return;
            }

            state->vuMeterScale = vuMeterScaleFromIndex(index);
            syncVuMeterScaleSelection();
            persistDisplayProperties();
            renderOnce();
        });
    pixelScaleDropdown->setOnSelectionChanged(
        [this](const int index)
        {
            if (!state)
            {
                return;
            }

            applyPixelScale(displayPixelScaleFromIndex(index));
            persistDisplayProperties();
            renderOnce();
        });

    window->setOnResize(
        [this]()
        {
            layoutComponents();
            renderOnce();
        });

    window->setRootComponent(std::move(rootComponent));

    layoutComponents();
    syncVuMeterScaleSelection();
    syncPixelScaleSelection();
    renderOnce();
    raise();
}

DisplaySettingsWindow::~DisplaySettingsWindow()
{
    if (window && state)
    {
        const auto it =
            std::find(state->windows.begin(), state->windows.end(), window.get());
        if (it != state->windows.end())
        {
            state->windows.erase(it);
        }
    }
}

void DisplaySettingsWindow::layoutComponents() const
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

    window->getRootComponent()->setSize(canvasWi, canvasHi);
    background->setBounds(0, 0, canvasWi, canvasHi);

    const int padding = scaleUi(state, 8.0f);
    const uint8_t labelFontPointSize =
        scaleFontPointSize(state, state->menuFontSize);
    const auto [labelTextW, labelTextH] =
        measureText("VU Meter Scale", labelFontPointSize);
    const auto [pixelScaleLabelW, pixelScaleLabelH] =
        measureText("Pixel Scale", labelFontPointSize);
    const int labelWidth =
        std::max(std::max(1, static_cast<int>(std::ceil(labelTextW))),
                 std::max(1, static_cast<int>(std::ceil(pixelScaleLabelW)))) +
        padding;
    const int rowHeight =
        std::max(std::max(1, static_cast<int>(std::ceil(labelTextH))),
                 std::max(1, static_cast<int>(std::ceil(pixelScaleLabelH)))) +
        padding * 2;
    const int dropdownX = padding + labelWidth + padding;
    const int dropdownW = canvasWi - dropdownX - padding;
    const int secondRowY = padding + rowHeight + padding;

    vuMeterScaleLabel->setBounds(padding, padding, labelWidth, rowHeight);
    vuMeterScaleDropdown->setBounds(dropdownX, padding, dropdownW, rowHeight);
    vuMeterScaleDropdown->setCollapsedHeight(rowHeight);
    vuMeterScaleDropdown->setItemMargin(padding);
    pixelScaleLabel->setBounds(padding, secondRowY, labelWidth, rowHeight);
    pixelScaleDropdown->setBounds(dropdownX, secondRowY, dropdownW, rowHeight);
    pixelScaleDropdown->setCollapsedHeight(rowHeight);
    pixelScaleDropdown->setItemMargin(padding);
}

void DisplaySettingsWindow::renderOnce() const
{
    if (window)
    {
        window->renderFrame();
    }
}

void DisplaySettingsWindow::syncVuMeterScaleSelection()
{
    if (!state)
    {
        return;
    }

    vuMeterScaleDropdown->setSelectedIndex(vuMeterScaleToIndex(state->vuMeterScale));

    if (!state->mainDocumentSessionWindow)
    {
        return;
    }

    auto *mainWindow = state->mainDocumentSessionWindow->getWindow();
    if (!mainWindow)
    {
        return;
    }

    if (auto *container = findComponentOfType<VuMeterContainer>(
            mainWindow->getRootComponent()))
    {
        container->syncScaleFromState();
    }

    if (auto *root = mainWindow->getRootComponent())
    {
        root->setDirty();
    }

    mainWindow->renderFrame();
}

void DisplaySettingsWindow::syncPixelScaleSelection()
{
    if (!state)
    {
        return;
    }

    pixelScaleDropdown->setSelectedIndex(displayPixelScaleToIndex(state->pixelScale));
}

void DisplaySettingsWindow::persistDisplayProperties() const
{
    if (!state)
    {
        return;
    }

    persistence::DisplayPropertiesPersistence::save(
        state->paths->displayPropertiesPath(),
        {.pixelScale = state->pixelScale, .vuMeterScale = state->vuMeterScale});
}

void DisplaySettingsWindow::applyPixelScale(const uint8_t newPixelScale)
{
    if (!state || !state->mainDocumentSessionWindow || newPixelScale == 0 ||
        state->pixelScale == newPixelScale)
    {
        syncPixelScaleSelection();
        return;
    }

    auto &viewState = state->getActiveViewState();
    const uint8_t oldPixelScale = state->pixelScale;
    state->pixelScale = newPixelScale;

    if (oldPixelScale > 0)
    {
        viewState.samplesPerPixel = adjustSamplesPerPixelForDisplayPixelScaleChange(
            viewState.samplesPerPixel, oldPixelScale, newPixelScale);
    }

    auto *mainWindow = state->mainDocumentSessionWindow->getWindow();
    buildComponents(state, mainWindow);
    for (auto *windowToRefresh : state->windows)
    {
        if (windowToRefresh)
        {
            windowToRefresh->refreshForScaleOrResize();
        }
    }
    refreshWaveforms(state, true, true);
    syncPixelScaleSelection();
}

void DisplaySettingsWindow::raise() const
{
    if (window && window->getSdlWindow())
    {
        SDL_RaiseWindow(window->getSdlWindow());
    }
}
