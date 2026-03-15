#include "DynamicsWindow.hpp"

#include "../State.hpp"
#include "../actions/audio/EffectCommands.hpp"
#include "Colors.hpp"
#include "Component.hpp"
#include "UiScale.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

using namespace cupuacu::gui;

namespace
{
    constexpr int kWindowWidth = 480;
    constexpr int kWindowHeight = 220;

    constexpr Uint32 getHighDensityWindowFlag()
    {
#if defined(__linux__)
        return 0;
#else
        return SDL_WINDOW_HIGH_PIXEL_DENSITY;
#endif
    }
} // namespace

DynamicsWindow::DynamicsWindow(State *stateToUse) : state(stateToUse)
{
    if (!state)
    {
        return;
    }

    thresholdPercent = state->effectSettings.dynamics.thresholdPercent;

    window = std::make_unique<Window>(
        state, "Dynamics", kWindowWidth, kWindowHeight,
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
            SDL_Log("DynamicsWindow: SDL_SetWindowParent failed: %s",
                    SDL_GetError());
        }
    }

    auto rootComponent = std::make_unique<Component>(state, "DynamicsRoot");
    rootComponent->setVisible(true);

    background =
        rootComponent->emplaceChild<OpaqueRect>(state, Colors::background);
    thresholdLabel = rootComponent->emplaceChild<Label>(state, "Threshold");
    ratioLabel = rootComponent->emplaceChild<Label>(state, "Ratio");
    thresholdInput = rootComponent->emplaceChild<TextInput>(state);
    thresholdSlider = rootComponent->emplaceChild<Slider>(
        state, [this]() { return thresholdPercent; },
        []() { return 0.0; }, []() { return 100.0; },
        [this](const double value) { setThresholdPercent(value); });
    ratioDropdown = rootComponent->emplaceChild<DropdownMenu>(state);
    resetButton = rootComponent->emplaceChild<TextButton>(state, "Reset");
    cancelButton = rootComponent->emplaceChild<TextButton>(state, "Cancel");
    applyButton = rootComponent->emplaceChild<TextButton>(state, "Apply");

    const int labelFontSize = static_cast<int>(state->menuFontSize);
    thresholdLabel->setFontSize(labelFontSize);
    ratioLabel->setFontSize(labelFontSize);
    thresholdInput->setFontSize(labelFontSize - 6);
    thresholdInput->setAllowedCharacters("0123456789.%");
    ratioDropdown->setFontSize(labelFontSize);
    ratioDropdown->setItems({"2:1", "4:1", "8:1", "Limiter"});
    ratioDropdown->setSelectedIndex(state->effectSettings.dynamics.ratioIndex);

    bindControls();
    syncInputs();
    syncSettings();

    window->setOnResize(
        [this]
        {
            layoutComponents();
            if (window)
            {
                window->renderFrameIfDirty();
            }
        });
    window->setOnClose(
        [this]
        {
            if (window && state)
            {
                const auto it =
                    std::find(state->windows.begin(), state->windows.end(),
                              window.get());
                if (it != state->windows.end())
                {
                    state->windows.erase(it);
                }
            }
        });

    window->setRootComponent(std::move(rootComponent));
    layoutComponents();
    window->renderFrame();
}

DynamicsWindow::~DynamicsWindow()
{
    if (window && state)
    {
        const auto it = std::find(state->windows.begin(), state->windows.end(),
                                  window.get());
        if (it != state->windows.end())
        {
            state->windows.erase(it);
        }
    }
}

void DynamicsWindow::raise() const
{
    if (window && window->getSdlWindow())
    {
        SDL_RaiseWindow(window->getSdlWindow());
    }
}

void DynamicsWindow::setThresholdPercent(const double value,
                                         const bool refreshInput)
{
    thresholdPercent = std::clamp(value, kMinPercent, kMaxPercent);
    if (thresholdSlider)
    {
        thresholdSlider->setDirty();
    }
    if (refreshInput && thresholdInput)
    {
        thresholdInput->setText(formatPercent(thresholdPercent));
    }
    syncSettings();
    if (window)
    {
        window->renderFrameIfDirty();
    }
}

void DynamicsWindow::syncSettings() const
{
    if (!state)
    {
        return;
    }

    auto &settings = state->effectSettings.dynamics;
    settings.thresholdPercent = thresholdPercent;
    settings.ratioIndex = ratioDropdown ? ratioDropdown->getSelectedIndex() : 0;
}

void DynamicsWindow::syncInputs()
{
    if (thresholdInput)
    {
        thresholdInput->setText(formatPercent(thresholdPercent));
    }
}

void DynamicsWindow::bindControls()
{
    thresholdInput->setOnTextChanged(
        [this](const std::string &text)
        {
            double parsed = thresholdPercent;
            if (tryParsePercent(text, parsed))
            {
                setThresholdPercent(parsed, false);
            }
        });
    thresholdInput->setOnEditingFinished(
        [this](const std::string &text)
        {
            double parsed = thresholdPercent;
            if (tryParsePercent(text, parsed))
            {
                setThresholdPercent(parsed);
            }
            else
            {
                thresholdInput->setText(formatPercent(thresholdPercent));
            }
        });

    ratioDropdown->setOnSelectionChanged(
        [this](const int)
        {
            syncSettings();
            if (window)
            {
                window->renderFrameIfDirty();
            }
        });

    resetButton->setOnPress([this]() { setDefaults(); });
    cancelButton->setTriggerOnMouseUp(true);
    applyButton->setTriggerOnMouseUp(true);
    cancelButton->setOnPress([this]() { closeNow(); });
    applyButton->setOnPress([this]() { applyEffect(); });
}

void DynamicsWindow::applyEffect()
{
    if (!state)
    {
        return;
    }

    double parsedThreshold = thresholdPercent;
    if (thresholdInput && tryParsePercent(thresholdInput->getText(), parsedThreshold))
    {
        setThresholdPercent(parsedThreshold);
    }

    cupuacu::actions::audio::performDynamics(
        state, thresholdPercent,
        ratioDropdown ? ratioDropdown->getSelectedIndex() : 0);
    closeNow();
}

void DynamicsWindow::setDefaults()
{
    setThresholdPercent(50.0);
    if (ratioDropdown)
    {
        ratioDropdown->setSelectedIndex(1);
    }
    syncSettings();
    if (window)
    {
        window->renderFrameIfDirty();
    }
}

void DynamicsWindow::layoutComponents() const
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
    const int padding = std::max(6, scaleUi(state, 14.0f));
    const int labelWidth = std::max(88, scaleUi(state, 106.0f));
    const int inputWidth = std::max(88, scaleUi(state, 100.0f));
    const int rowHeight = std::max(30, scaleUi(state, 34.0f));
    const int sliderHeight = std::max(22, scaleUi(state, 24.0f));
    const int sliderX = padding + labelWidth + inputWidth + padding * 2;
    const int sliderWidth = std::max(80, canvasWi - sliderX - padding);

    window->getRootComponent()->setSize(canvasWi, canvasHi);
    background->setBounds(0, 0, canvasWi, canvasHi);

    int y = padding;
    thresholdLabel->setBounds(padding, y, labelWidth, rowHeight);
    thresholdInput->setBounds(padding + labelWidth, y, inputWidth, rowHeight);
    thresholdSlider->setBounds(sliderX, y, sliderWidth, sliderHeight);

    y += rowHeight + padding * 2;
    ratioLabel->setBounds(padding, y, labelWidth, rowHeight);
    ratioDropdown->setBounds(padding + labelWidth, y,
                             canvasWi - (padding + labelWidth) - padding,
                             std::max(rowHeight, ratioDropdown->getRowHeight()));
    ratioDropdown->setCollapsedHeight(
        std::max(rowHeight, ratioDropdown->getRowHeight()));

    const int buttonWidth = std::max(96, scaleUi(state, 120.0f));
    const int bottomY = canvasHi - padding - rowHeight;
    applyButton->setBounds(canvasWi - padding - buttonWidth, bottomY, buttonWidth,
                           rowHeight);
    cancelButton->setBounds(canvasWi - padding * 2 - buttonWidth * 2, bottomY,
                            buttonWidth, rowHeight);
    resetButton->setBounds(padding, bottomY, buttonWidth, rowHeight);
}

void DynamicsWindow::closeNow()
{
    if (window)
    {
        window->requestClose();
    }
}

std::string DynamicsWindow::formatPercent(const double value)
{
    std::ostringstream stream;
    const double rounded = std::round(value);
    if (std::fabs(value - rounded) < 1e-9)
    {
        stream << static_cast<long long>(std::llround(rounded));
    }
    else
    {
        stream << std::fixed << std::setprecision(2) << value;
        auto formatted = stream.str();
        while (!formatted.empty() && formatted.back() == '0')
        {
            formatted.pop_back();
        }
        if (!formatted.empty() && formatted.back() == '.')
        {
            formatted.pop_back();
        }
        return formatted + "%";
    }
    return stream.str() + "%";
}

bool DynamicsWindow::tryParsePercent(const std::string &text, double &valueOut)
{
    std::string sanitized;
    sanitized.reserve(text.size());
    for (const char c : text)
    {
        if (c != '%')
        {
            sanitized.push_back(c);
        }
    }

    if (sanitized.empty())
    {
        return false;
    }

    try
    {
        std::size_t consumed = 0;
        const double parsed = std::stod(sanitized, &consumed);
        if (consumed != sanitized.size() || !std::isfinite(parsed))
        {
            return false;
        }
        valueOut = parsed;
        return true;
    }
    catch (...)
    {
        return false;
    }
}
