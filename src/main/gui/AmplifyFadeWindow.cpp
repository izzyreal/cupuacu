#include "AmplifyFadeWindow.hpp"

#include "../State.hpp"
#include "../actions/audio/EffectCommands.hpp"
#include "Colors.hpp"
#include "Component.hpp"
#include "UiScale.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>

using namespace cupuacu::gui;

namespace
{
    constexpr int kWindowWidth = 560;
    constexpr int kWindowHeight = 320;

    constexpr Uint32 getHighDensityWindowFlag()
    {
#if defined(__linux__)
        return 0;
#else
        return SDL_WINDOW_HIGH_PIXEL_DENSITY;
#endif
    }
} // namespace

AmplifyFadeWindow::AmplifyFadeWindow(State *stateToUse) : state(stateToUse)
{
    if (!state)
    {
        return;
    }

    startPercent = state->effectSettings.amplifyFade.startPercent;
    endPercent = state->effectSettings.amplifyFade.endPercent;
    lockEnabled = state->effectSettings.amplifyFade.lockEnabled;

    window = std::make_unique<Window>(
        state, "Amplify/Fade", kWindowWidth, kWindowHeight,
        SDL_WINDOW_RESIZABLE | getHighDensityWindowFlag());
    if (!window->isOpen())
    {
        return;
    }

    state->windows.push_back(window.get());

    auto *mainWindow = state->mainDocumentSessionWindow->getWindow();
    if (mainWindow && mainWindow->getSdlWindow())
    {
        if (!SDL_SetWindowParent(window->getSdlWindow(),
                                 mainWindow->getSdlWindow()))
        {
            SDL_Log("AmplifyFadeWindow: SDL_SetWindowParent failed: %s",
                    SDL_GetError());
        }
    }

    auto rootComponent = std::make_unique<Component>(state, "AmplifyFadeRoot");
    rootComponent->setVisible(true);

    background =
        rootComponent->emplaceChild<OpaqueRect>(state, Colors::background);

    startLabel = rootComponent->emplaceChild<Label>(state, "Start");
    endLabel = rootComponent->emplaceChild<Label>(state, "End");
    curveLabel = rootComponent->emplaceChild<Label>(state, "Curve");

    startInput = rootComponent->emplaceChild<TextInput>(state);
    endInput = rootComponent->emplaceChild<TextInput>(state);

    startSlider = rootComponent->emplaceChild<Slider>(
        state, [this]() { return startPercent; },
        []() { return 0.0; }, []() { return 1000.0; },
        [this](const double value) { setStartPercent(value); });
    endSlider = rootComponent->emplaceChild<Slider>(
        state, [this]() { return endPercent; },
        []() { return 0.0; }, []() { return 1000.0; },
        [this](const double value) { setEndPercent(value); });

    lockButton =
        rootComponent->emplaceChild<TextButton>(state, "Lock", ButtonType::Toggle);
    curveDropdown = rootComponent->emplaceChild<DropdownMenu>(state);
    resetButton = rootComponent->emplaceChild<TextButton>(state, "Reset");
    fadeInButton = rootComponent->emplaceChild<TextButton>(state, "Fade in");
    fadeOutButton = rootComponent->emplaceChild<TextButton>(state, "Fade out");
    cancelButton = rootComponent->emplaceChild<TextButton>(state, "Cancel");
    applyButton = rootComponent->emplaceChild<TextButton>(state, "Apply");

    const int labelFontSize = static_cast<int>(state->menuFontSize);
    startLabel->setFontSize(labelFontSize);
    endLabel->setFontSize(labelFontSize);
    curveLabel->setFontSize(labelFontSize);
    startInput->setFontSize(labelFontSize - 6);
    endInput->setFontSize(labelFontSize - 6);
    startInput->setAllowedCharacters("0123456789.%");
    endInput->setAllowedCharacters("0123456789.%");
    curveDropdown->setFontSize(labelFontSize);
    curveDropdown->setItems({"Linear", "Exponential", "Logarithmic"});
    curveDropdown->setSelectedIndex(state->effectSettings.amplifyFade.curveIndex);
    curveDropdown->setExpanded(false);
    updateLockButton();

    bindTextInputs();
    bindButtons();
    bindDropdown();
    syncInputs();
    syncSettings();

    window->setOnResize(
        [this]
        {
            layoutComponents();
            renderOnce();
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

            if (state && state->mainDocumentSessionWindow)
            {
                auto *mainWindow = state->mainDocumentSessionWindow->getWindow();
                if (mainWindow && mainWindow->getSdlWindow())
                {
                    SDL_RaiseWindow(mainWindow->getSdlWindow());
                    mainWindow->updateHoverFromCurrentMousePosition();
                    mainWindow->renderFrameIfDirty();
                }
            }
        });

    window->setRootComponent(std::move(rootComponent));
    layoutComponents();
    curveDropdown->setSelectedIndex(state->effectSettings.amplifyFade.curveIndex);
    renderOnce();
    raise();
}

AmplifyFadeWindow::~AmplifyFadeWindow()
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

    window.reset();
}

void AmplifyFadeWindow::raise() const
{
    if (window && window->getSdlWindow())
    {
        SDL_RaiseWindow(window->getSdlWindow());
    }
}

bool AmplifyFadeWindow::updatePercentControl(double &currentPercent,
                                             Slider *slider, TextInput *input,
                                             const double value,
                                             const bool refreshInput)
{
    const double clamped = std::clamp(value, kMinPercent, kMaxPercent);
    const bool changed = std::fabs(currentPercent - clamped) >= 1e-9;
    currentPercent = clamped;

    if (slider)
    {
        slider->setDirty();
    }
    if (refreshInput && input)
    {
        input->setText(formatPercent(currentPercent));
    }

    return changed || refreshInput;
}

void AmplifyFadeWindow::syncSettings() const
{
    if (!state)
    {
        return;
    }

    auto &settings = state->effectSettings.amplifyFade;
    settings.startPercent = startPercent;
    settings.endPercent = endPercent;
    settings.lockEnabled = lockEnabled;
    settings.curveIndex = getCurveIndex();
}

void AmplifyFadeWindow::setStartPercent(const double value,
                                        const bool refreshInput)
{
    bool shouldRender =
        updatePercentControl(startPercent, startSlider, startInput, value,
                             refreshInput);
    if (lockEnabled && !syncingLockedValues)
    {
        syncingLockedValues = true;
        shouldRender =
            updatePercentControl(endPercent, endSlider, endInput, startPercent,
                                 refreshInput) ||
            shouldRender;
        syncingLockedValues = false;
    }
    syncSettings();
    if (shouldRender)
    {
        renderOnce();
    }
}

void AmplifyFadeWindow::setEndPercent(const double value,
                                      const bool refreshInput)
{
    bool shouldRender =
        updatePercentControl(endPercent, endSlider, endInput, value,
                             refreshInput);
    if (lockEnabled && !syncingLockedValues)
    {
        syncingLockedValues = true;
        shouldRender =
            updatePercentControl(startPercent, startSlider, startInput, endPercent,
                                 refreshInput) ||
            shouldRender;
        syncingLockedValues = false;
    }
    syncSettings();
    if (shouldRender)
    {
        renderOnce();
    }
}

void AmplifyFadeWindow::setLocked(const bool enabled)
{
    lockEnabled = enabled;
    if (lockButton)
    {
        lockButton->setToggled(lockEnabled);
    }
    updateLockButton();
    if (lockEnabled)
    {
        setEndPercent(startPercent);
        return;
    }

    syncSettings();
    renderOnce();
}

void AmplifyFadeWindow::updateLockButton()
{
    if (lockButton)
    {
        lockButton->setText("Lock");
    }
}

void AmplifyFadeWindow::commitInputValues()
{
    double parsedStart = startPercent;
    if (tryParsePercent(startInput->getText(), parsedStart))
    {
        setStartPercent(parsedStart);
    }
    else
    {
        startInput->setText(formatPercent(startPercent));
    }

    if (!lockEnabled)
    {
        double parsedEnd = endPercent;
        if (tryParsePercent(endInput->getText(), parsedEnd))
        {
            setEndPercent(parsedEnd);
        }
        else
        {
            endInput->setText(formatPercent(endPercent));
        }
    }

    syncSettings();
    renderOnce();
}

void AmplifyFadeWindow::closeNow()
{
    if (window)
    {
        window->requestClose();
    }
}

void AmplifyFadeWindow::applyEffect()
{
    if (!state)
    {
        return;
    }

    commitInputValues();
    if (state->mainDocumentSessionWindow)
    {
        const auto &viewState = state->mainDocumentSessionWindow->getViewState();
        const auto &selection = state->activeDocumentSession.selection;
        SDL_Log(
            "CUPUACU_DEBUG_FADE_BOUNDARY: apply start=%.2f end=%.2f curve=%d pixelScale=%u samplesPerPixel=%.6f sampleOffset=%lld selectionActive=%d selection=[%lld,%lld)",
            startPercent, endPercent, getCurveIndex(),
            static_cast<unsigned>(state->pixelScale), viewState.samplesPerPixel,
            static_cast<long long>(viewState.sampleOffset),
            selection.isActive() ? 1 : 0,
            static_cast<long long>(selection.isActive() ? selection.getStartInt()
                                                        : 0),
            static_cast<long long>(
                selection.isActive() ? selection.getEndExclusiveInt() : 0));
    }
    cupuacu::actions::audio::performAmplifyFade(
        state, startPercent, endPercent, getCurveIndex());

    if (state->mainDocumentSessionWindow &&
        state->mainDocumentSessionWindow->getWindow())
    {
        state->mainDocumentSessionWindow->getWindow()->renderFrameIfDirty();
    }
    closeNow();
}

void AmplifyFadeWindow::setDefaults()
{
    setStartPercent(100.0);
    setEndPercent(100.0);
    if (curveDropdown)
    {
        curveDropdown->setSelectedIndex(0);
    }
    syncSettings();
    renderOnce();
}

void AmplifyFadeWindow::applyFadeInPreset()
{
    setStartPercent(0.0);
    setEndPercent(100.0);
    renderOnce();
}

void AmplifyFadeWindow::applyFadeOutPreset()
{
    setStartPercent(100.0);
    setEndPercent(0.0);
    renderOnce();
}

void AmplifyFadeWindow::bindTextInputs()
{
    startInput->setOnTextChanged(
        [this](const std::string &text)
        {
            double parsed = startPercent;
            if (tryParsePercent(text, parsed))
            {
                setStartPercent(parsed, false);
            }
        });
    startInput->setOnEditingFinished(
        [this](const std::string &text)
        {
            double parsed = startPercent;
            if (tryParsePercent(text, parsed))
            {
                setStartPercent(parsed);
            }
            else
            {
                startInput->setText(formatPercent(startPercent));
            }
        });

    endInput->setOnTextChanged(
        [this](const std::string &text)
        {
            double parsed = endPercent;
            if (tryParsePercent(text, parsed))
            {
                setEndPercent(parsed, false);
            }
        });
    endInput->setOnEditingFinished(
        [this](const std::string &text)
        {
            double parsed = endPercent;
            if (tryParsePercent(text, parsed))
            {
                setEndPercent(parsed);
            }
            else
            {
                endInput->setText(formatPercent(endPercent));
            }
        });
}

void AmplifyFadeWindow::bindButtons()
{
    resetButton->setOnPress([this]() { setDefaults(); });
    fadeInButton->setOnPress([this]() { applyFadeInPreset(); });
    fadeOutButton->setOnPress([this]() { applyFadeOutPreset(); });
    lockButton->setOnToggle([this](const bool isEnabled) { setLocked(isEnabled); });
    cancelButton->setOnPress([this]() { closeNow(); });
    applyButton->setOnPress([this]() { applyEffect(); });
}

void AmplifyFadeWindow::bindDropdown()
{
    curveDropdown->setOnSelectionChanged(
        [this](const int)
        {
            syncSettings();
            renderOnce();
        });
}

void AmplifyFadeWindow::layoutComponents() const
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

    const int padding = std::max(4, scaleUi(state, 12.0f));
    const int labelWidth = std::max(76, scaleUi(state, 92.0f));
    const int inputWidth = std::max(90, scaleUi(state, 110.0f));
    const int rowHeight = std::max(30, scaleUi(state, 34.0f));
    const int lockButtonWidth = std::max(48, scaleUi(state, 60.0f));
    const int lockButtonHeight = std::max(28, scaleUi(state, 34.0f));
    const int sliderHeight = std::max(22, scaleUi(state, 24.0f));
    const int sliderX =
        padding + labelWidth + inputWidth + lockButtonWidth + padding * 3;
    const int sliderWidth = std::max(80, canvasWi - sliderX - padding);

    int y = padding;

    startLabel->setBounds(padding, y, labelWidth, rowHeight);
    startInput->setBounds(padding + labelWidth, y, inputWidth, rowHeight);
    startSlider->setBounds(sliderX, y, sliderWidth, sliderHeight);

    y += rowHeight + padding;

    endLabel->setBounds(padding, y, labelWidth, rowHeight);
    endInput->setBounds(padding + labelWidth, y, inputWidth, rowHeight);
    endSlider->setBounds(sliderX, y, sliderWidth, sliderHeight);

    const int lockButtonX = padding + labelWidth + inputWidth + padding;
    const int lockCenterY = padding + rowHeight + padding / 2;
    lockButton->setBounds(lockButtonX, lockCenterY - lockButtonHeight / 2,
                          lockButtonWidth, lockButtonHeight);

    y += rowHeight + padding * 2;

    curveDropdown->setItemMargin(std::max(4, padding / 2));
    const int dropdownHeight = std::max(rowHeight, curveDropdown->getRowHeight());
    const int curveRowHeight = std::max(rowHeight, dropdownHeight);
    curveLabel->setBounds(padding, y, labelWidth, curveRowHeight);
    curveDropdown->setBounds(padding + labelWidth + padding,
                             y + (curveRowHeight - dropdownHeight) / 2,
                             canvasWi - (padding + labelWidth + padding) - padding,
                             dropdownHeight);
    curveDropdown->setCollapsedHeight(dropdownHeight);

    y += curveRowHeight + padding * 2;

    const int buttonGap = padding;
    const int buttonWidth =
        std::max(80, (canvasWi - padding * 2 - buttonGap * 2) / 3);

    resetButton->setBounds(padding, y, buttonWidth, rowHeight);
    fadeInButton->setBounds(padding + buttonWidth + buttonGap, y, buttonWidth,
                            rowHeight);
    fadeOutButton->setBounds(padding + (buttonWidth + buttonGap) * 2, y,
                             buttonWidth, rowHeight);

    const int bottomButtonWidth = std::max(96, scaleUi(state, 120.0f));
    const int bottomButtonY = canvasHi - padding - rowHeight;
    applyButton->setBounds(canvasWi - padding - bottomButtonWidth, bottomButtonY,
                           bottomButtonWidth, rowHeight);
    cancelButton->setBounds(canvasWi - padding * 2 - bottomButtonWidth * 2,
                            bottomButtonY, bottomButtonWidth, rowHeight);
}

void AmplifyFadeWindow::renderOnce() const
{
    if (window)
    {
        window->renderFrameIfDirty();
    }
}

void AmplifyFadeWindow::syncInputs()
{
    if (startInput)
    {
        startInput->setText(formatPercent(startPercent));
    }
    if (endInput)
    {
        endInput->setText(formatPercent(endPercent));
    }
}

std::string AmplifyFadeWindow::formatPercent(const double value)
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

bool AmplifyFadeWindow::tryParsePercent(const std::string &text, double &valueOut)
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
