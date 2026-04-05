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

DisplaySettingsPane::DisplaySettingsPane(State *stateToUse)
    : Component(stateToUse, "OptionsDisplayPane")
{
    background = emplaceChild<OpaqueRect>(state, Colors::background);
    vuMeterScaleLabel = emplaceChild<Label>(state, "VU Meter Scale");
    vuMeterScaleDropdown = emplaceChild<DropdownMenu>(state);
    pixelScaleLabel = emplaceChild<Label>(state, "Pixel Scale");
    pixelScaleDropdown = emplaceChild<DropdownMenu>(state);

    const int labelFontSize = static_cast<int>(state->menuFontSize);
    vuMeterScaleLabel->setFontSize(labelFontSize);
    vuMeterScaleDropdown->setFontSize(labelFontSize);
    pixelScaleLabel->setFontSize(labelFontSize);
    pixelScaleDropdown->setFontSize(labelFontSize);
    vuMeterScaleDropdown->setExpanded(false);
    vuMeterScaleDropdown->setItems(vuMeterScaleOptionLabels());
    vuMeterScaleDropdown->setSelectedIndex(
        vuMeterScaleToIndex(state->vuMeterScale));
    pixelScaleDropdown->setExpanded(false);
    pixelScaleDropdown->setItems(displayPixelScaleOptionLabels());
    pixelScaleDropdown->setSelectedIndex(
        displayPixelScaleToIndex(state->pixelScale));
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
            if (window)
            {
                window->renderFrameIfDirty();
            }
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
            if (window)
            {
                window->renderFrameIfDirty();
            }
        });

    syncVuMeterScaleSelection();
    syncPixelScaleSelection();
}

void DisplaySettingsPane::resized()
{
    layoutComponents();
}

void DisplaySettingsPane::layoutComponents() const
{
    const int canvasWi = getWidth();
    const int canvasHi = getHeight();

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
    const int dropdownW = std::max(1, canvasWi - dropdownX - padding);
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

void DisplaySettingsPane::syncVuMeterScaleSelection()
{
    if (!state)
    {
        return;
    }

    vuMeterScaleDropdown->setSelectedIndex(
        vuMeterScaleToIndex(state->vuMeterScale));

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

void DisplaySettingsPane::syncPixelScaleSelection()
{
    if (!state)
    {
        return;
    }

    pixelScaleDropdown->setSelectedIndex(
        displayPixelScaleToIndex(state->pixelScale));
}

void DisplaySettingsPane::persistDisplayProperties() const
{
    if (!state)
    {
        return;
    }

    persistence::DisplayPropertiesPersistence::save(
        state->paths->displayPropertiesPath(),
        {.pixelScale = state->pixelScale, .vuMeterScale = state->vuMeterScale});
}

void DisplaySettingsPane::applyPixelScale(const uint8_t newPixelScale)
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
