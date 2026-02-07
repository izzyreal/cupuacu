#include "DevicePropertiesWindow.h"

#include "../PaUtil.hpp"
#include "../AudioDevices.hpp"
#include "Colors.h"
#include "text.h"

#include <portaudio.h>
#include <algorithm>
#include <cmath>

using namespace cupuacu::gui;

namespace
{
    constexpr int kWindowWidth = 500;
    constexpr int kWindowHeight = 500;

    int findIndex(const std::vector<int> &indices, const int value)
    {
        const auto it = std::find(indices.begin(), indices.end(), value);
        return it == indices.end()
                   ? -1
                   : static_cast<int>(it - indices.begin());
    }

    int getDefaultDeviceIndexForHostApi(const int hostApiIndex,
                                        const bool isInput)
    {
        if (hostApiIndex < 0)
        {
            return -1;
        }
        const PaHostApiInfo *info = Pa_GetHostApiInfo(hostApiIndex);
        if (!info)
        {
            return -1;
        }
        return isInput ? info->defaultInputDevice : info->defaultOutputDevice;
    }
} // namespace

DevicePropertiesWindow::DevicePropertiesWindow(State *stateToUse)
    : state(stateToUse)
{
    if (!state)
    {
        return;
    }

    window = std::make_unique<Window>(
        state, "Device Properties", kWindowWidth, kWindowHeight,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window->isOpen())
    {
        return;
    }

    if (state)
    {
        state->windows.push_back(window.get());
    }

    if (state->mainWindow && state->mainWindow->getSdlWindow())
    {
        if (!SDL_SetWindowParent(window->getSdlWindow(),
                                 state->mainWindow->getSdlWindow()))
        {
            SDL_Log("DevicePropertiesWindow: SDL_SetWindowParent failed: %s",
                    SDL_GetError());
        }
    }

    auto rootComponent =
        std::make_unique<Component>(state, "DevicePropertiesRoot");
    rootComponent->setVisible(true);

    background =
        rootComponent->emplaceChild<OpaqueRect>(state, Colors::background);

    deviceTypeLabel = rootComponent->emplaceChild<Label>(state, "Device Type");
    deviceTypeDropdown = rootComponent->emplaceChild<DropdownMenu>(state);

    outputDeviceLabel =
        rootComponent->emplaceChild<Label>(state, "Output Device");
    outputDeviceDropdown = rootComponent->emplaceChild<DropdownMenu>(state);

    inputDeviceLabel =
        rootComponent->emplaceChild<Label>(state, "Input Device");
    inputDeviceDropdown = rootComponent->emplaceChild<DropdownMenu>(state);

    const int labelFontSize = static_cast<int>(state->menuFontSize);

    deviceTypeLabel->setFontSize(labelFontSize);
    outputDeviceLabel->setFontSize(labelFontSize);
    inputDeviceLabel->setFontSize(labelFontSize);
    deviceTypeDropdown->setFontSize(labelFontSize);
    outputDeviceDropdown->setFontSize(labelFontSize);
    inputDeviceDropdown->setFontSize(labelFontSize);

    deviceTypeDropdown->setExpanded(false);
    outputDeviceDropdown->setExpanded(false);
    inputDeviceDropdown->setExpanded(false);

    populateHostApis();

    int preferredHostApiIndex = -1;
    int preferredOutputDeviceIndex = -1;
    int preferredInputDeviceIndex = -1;
    if (state->audioDevices)
    {
        const auto selection = state->audioDevices->getDeviceSelection();
        preferredHostApiIndex = selection.hostApiIndex;
        preferredOutputDeviceIndex = selection.outputDeviceIndex;
        preferredInputDeviceIndex = selection.inputDeviceIndex;
    }

    int hostApiDropdownIndex = findIndex(hostApiIndices, preferredHostApiIndex);
    if (hostApiDropdownIndex < 0)
    {
        hostApiDropdownIndex = 0;
    }
    deviceTypeDropdown->setSelectedIndex(hostApiDropdownIndex);

    const int hostApiIndex =
        hostApiDropdownIndex >= 0 &&
                hostApiDropdownIndex < (int)hostApiIndices.size()
            ? hostApiIndices[hostApiDropdownIndex]
            : -1;
    populateDevices(hostApiIndex, preferredOutputDeviceIndex,
                    preferredInputDeviceIndex);
    syncSelectionToAudioDevices();

    deviceTypeDropdown->setOnSelectionChanged(
        [this](const int index)
        {
            int hostApiIndexToUse = -1;
            if (index >= 0 && index < (int)hostApiIndices.size())
            {
                hostApiIndexToUse = hostApiIndices[index];
            }
            int preferredOutputDeviceIndex = -1;
            int preferredInputDeviceIndex = -1;
            if (state && state->audioDevices)
            {
                const auto selection =
                    state->audioDevices->getDeviceSelection();
                preferredOutputDeviceIndex = selection.outputDeviceIndex;
                preferredInputDeviceIndex = selection.inputDeviceIndex;
            }
            populateDevices(hostApiIndexToUse, preferredOutputDeviceIndex,
                            preferredInputDeviceIndex);
            syncSelectionToAudioDevices();
            layoutComponents();
            renderOnce();
        });
    outputDeviceDropdown->setOnSelectionChanged(
        [this](const int)
        {
            syncSelectionToAudioDevices();
        });
    inputDeviceDropdown->setOnSelectionChanged(
        [this](const int)
        {
            syncSelectionToAudioDevices();
        });

    window->setOnResize(
        [this]
        {
            layoutComponents();
            renderOnce();
        });
    window->setOnClose(
        [this]
        {
            if (state && state->mainWindow && state->mainWindow->getSdlWindow())
            {
                SDL_RaiseWindow(state->mainWindow->getSdlWindow());
            }
        });

    window->setRootComponent(std::move(rootComponent));

    layoutComponents();
    renderOnce();

    raise();
}

DevicePropertiesWindow::~DevicePropertiesWindow()
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

    if (ownsPortAudio)
    {
        Pa_Terminate();
    }
}

void DevicePropertiesWindow::populateHostApis()
{
    int apiCount = Pa_GetHostApiCount();
    if (apiCount == paNotInitialized)
    {
        PaError err = Pa_Initialize();
        if (err != paNoError)
        {
            PaUtil::handlePaError(err);
            apiCount = -1;
        }
        else
        {
            ownsPortAudio = true;
            apiCount = Pa_GetHostApiCount();
        }
    }

    std::vector<std::string> apiNames;
    hostApiIndices.clear();

    if (apiCount < 0)
    {
        apiNames.emplace_back("No host APIs");
    }
    else
    {
        for (int i = 0; i < apiCount; ++i)
        {
            const PaHostApiInfo *info = Pa_GetHostApiInfo(i);
            if (!info || !info->name)
            {
                continue;
            }
            apiNames.emplace_back(info->name);
            hostApiIndices.push_back(i);
        }
    }

    if (apiNames.empty())
    {
        apiNames.emplace_back("No host APIs");
    }

    deviceTypeDropdown->setItems(apiNames);
    deviceTypeDropdown->setSelectedIndex(0);
}

void DevicePropertiesWindow::populateDevices(
    const int hostApiIndex, const int preferredOutputDeviceIndex,
    const int preferredInputDeviceIndex)
{
    outputDeviceIndices.clear();
    inputDeviceIndices.clear();

    std::vector<std::string> outputItems;
    std::vector<std::string> inputItems;

    const int deviceCount = Pa_GetDeviceCount();
    if (deviceCount < 0)
    {
        outputItems.emplace_back("No output devices");
        inputItems.emplace_back("No input devices");
    }
    else
    {
        for (int i = 0; i < deviceCount; ++i)
        {
            const PaDeviceInfo *info = Pa_GetDeviceInfo(i);
            if (!info)
            {
                continue;
            }

            if (hostApiIndex >= 0 && info->hostApi != hostApiIndex)
            {
                continue;
            }

            if (info->maxOutputChannels > 0)
            {
                outputItems.emplace_back(info->name ? info->name
                                                    : "Unknown output device");
                outputDeviceIndices.push_back(i);
            }

            if (info->maxInputChannels > 0)
            {
                inputItems.emplace_back(info->name ? info->name
                                                   : "Unknown input device");
                inputDeviceIndices.push_back(i);
            }
        }
    }

    if (outputItems.empty())
    {
        outputItems.emplace_back("No output devices");
    }

    if (inputItems.empty())
    {
        inputItems.emplace_back("No input devices");
    }

    outputDeviceDropdown->setItems(outputItems);
    int outputSelectionIndex =
        findIndex(outputDeviceIndices, preferredOutputDeviceIndex);
    if (outputSelectionIndex < 0)
    {
        const int defaultOutputDeviceIndex =
            getDefaultDeviceIndexForHostApi(hostApiIndex, false);
        outputSelectionIndex =
            findIndex(outputDeviceIndices, defaultOutputDeviceIndex);
    }
    if (outputSelectionIndex < 0)
    {
        outputSelectionIndex = 0;
    }
    outputDeviceDropdown->setSelectedIndex(outputSelectionIndex);

    inputDeviceDropdown->setItems(inputItems);
    int inputSelectionIndex =
        findIndex(inputDeviceIndices, preferredInputDeviceIndex);
    if (inputSelectionIndex < 0)
    {
        const int defaultInputDeviceIndex =
            getDefaultDeviceIndexForHostApi(hostApiIndex, true);
        inputSelectionIndex =
            findIndex(inputDeviceIndices, defaultInputDeviceIndex);
    }
    if (inputSelectionIndex < 0)
    {
        inputSelectionIndex = 0;
    }
    inputDeviceDropdown->setSelectedIndex(inputSelectionIndex);
}

int DevicePropertiesWindow::getSelectedHostApiIndex() const
{
    if (!deviceTypeDropdown)
    {
        return -1;
    }
    const int selectedIndex = deviceTypeDropdown->getSelectedIndex();
    if (selectedIndex >= 0 && selectedIndex < (int)hostApiIndices.size())
    {
        return hostApiIndices[selectedIndex];
    }
    return -1;
}

int DevicePropertiesWindow::getSelectedDeviceIndex(
    const DropdownMenu *dropdown, const std::vector<int> &indices) const
{
    if (!dropdown)
    {
        return -1;
    }
    const int selectedIndex = dropdown->getSelectedIndex();
    if (selectedIndex >= 0 && selectedIndex < (int)indices.size())
    {
        return indices[selectedIndex];
    }
    return -1;
}

void DevicePropertiesWindow::syncSelectionToAudioDevices()
{
    if (!state || !state->audioDevices)
    {
        return;
    }

    cupuacu::AudioDevices::DeviceSelection selection;
    selection.hostApiIndex = getSelectedHostApiIndex();
    selection.outputDeviceIndex =
        getSelectedDeviceIndex(outputDeviceDropdown, outputDeviceIndices);
    selection.inputDeviceIndex =
        getSelectedDeviceIndex(inputDeviceDropdown, inputDeviceIndices);

    state->audioDevices->setDeviceSelection(selection);
}

void DevicePropertiesWindow::layoutComponents() const
{
    if (!window || !window->getRootComponent())
    {
        return;
    }

    float canvasW = (float)kWindowWidth;
    float canvasH = (float)kWindowHeight;
    if (window->getCanvas())
    {
        SDL_GetTextureSize(window->getCanvas(), &canvasW, &canvasH);
    }

    const int canvasWi = (int)canvasW;
    const int canvasHi = (int)canvasH;

    window->getRootComponent()->setSize(canvasWi, canvasHi);
    background->setBounds(0, 0, canvasWi, canvasHi);

    const int padding = std::max(1, 8 / state->pixelScale);
    const auto [labelTextW, labelTextH] =
        measureText("Output Device", state->menuFontSize);
    const int labelWidth =
        std::max(1, (int)std::ceil(labelTextW / state->pixelScale)) + padding;
    const int rowHeight =
        std::max(1, (int)std::ceil(labelTextH / state->pixelScale)) +
        padding * 2;

    const int dropdownX = padding + labelWidth + padding;
    const int dropdownW = canvasWi - dropdownX - padding;

    int y = padding;

    deviceTypeLabel->setBounds(padding, y, labelWidth, rowHeight);
    deviceTypeDropdown->setBounds(dropdownX, y, dropdownW, rowHeight);
    deviceTypeDropdown->setCollapsedHeight(rowHeight);
    deviceTypeDropdown->setItemMargin(padding);

    y += rowHeight + padding;

    outputDeviceLabel->setBounds(padding, y, labelWidth, rowHeight);
    outputDeviceDropdown->setBounds(dropdownX, y, dropdownW, rowHeight);
    outputDeviceDropdown->setCollapsedHeight(rowHeight);
    outputDeviceDropdown->setItemMargin(padding);

    y += rowHeight + padding;

    inputDeviceLabel->setBounds(padding, y, labelWidth, rowHeight);
    inputDeviceDropdown->setBounds(dropdownX, y, dropdownW, rowHeight);
    inputDeviceDropdown->setCollapsedHeight(rowHeight);
    inputDeviceDropdown->setItemMargin(padding);
}

void DevicePropertiesWindow::renderOnce() const
{
    if (window)
    {
        window->renderFrame();
    }
}

void DevicePropertiesWindow::raise() const
{
    if (window && window->getSdlWindow())
    {
        SDL_RaiseWindow(window->getSdlWindow());
    }
}
