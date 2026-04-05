#include "DevicePropertiesWindow.hpp"

#include "../PaUtil.hpp"
#include "../audio/AudioDevices.hpp"
#include "../persistence/AudioDevicePropertiesPersistence.hpp"
#include "Colors.hpp"
#include "text.hpp"

#include <algorithm>
#include <cmath>
#include <portaudio.h>

using namespace cupuacu::gui;

namespace
{
    int findIndex(const std::vector<int> &indices, const int value)
    {
        const auto it = std::find(indices.begin(), indices.end(), value);
        return it == indices.end() ? -1
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

DevicePropertiesPane::DevicePropertiesPane(State *stateToUse)
    : Component(stateToUse, "OptionsAudioPane")
{
    background = emplaceChild<OpaqueRect>(state, Colors::background);

    deviceTypeLabel = emplaceChild<Label>(state, "Device Type");
    deviceTypeDropdown = emplaceChild<DropdownMenu>(state);

    outputDeviceLabel = emplaceChild<Label>(state, "Output Device");
    outputDeviceDropdown = emplaceChild<DropdownMenu>(state);

    inputDeviceLabel = emplaceChild<Label>(state, "Input Device");
    inputDeviceDropdown = emplaceChild<DropdownMenu>(state);

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
    if (state && state->audioDevices)
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
                hostApiDropdownIndex < static_cast<int>(hostApiIndices.size())
            ? hostApiIndices[hostApiDropdownIndex]
            : -1;
    populateDevices(hostApiIndex, preferredOutputDeviceIndex,
                    preferredInputDeviceIndex);
    if (syncSelectionToAudioDevices())
    {
        persistence::AudioDevicePropertiesPersistence::save(
            state->paths->audioDevicePropertiesPath(),
            state->audioDevices->getDeviceSelection());
    }

    deviceTypeDropdown->setOnSelectionChanged(
        [this](const int index)
        {
            int hostApiIndexToUse = -1;
            if (index >= 0 && index < static_cast<int>(hostApiIndices.size()))
            {
                hostApiIndexToUse = hostApiIndices[index];
            }
            int preferredOutputDeviceIndex = -1;
            int preferredInputDeviceIndex = -1;
            if (state && state->audioDevices)
            {
                const auto selection = state->audioDevices->getDeviceSelection();
                preferredOutputDeviceIndex = selection.outputDeviceIndex;
                preferredInputDeviceIndex = selection.inputDeviceIndex;
            }
            populateDevices(hostApiIndexToUse, preferredOutputDeviceIndex,
                            preferredInputDeviceIndex);
            if (syncSelectionToAudioDevices())
            {
                persistence::AudioDevicePropertiesPersistence::save(
                    state->paths->audioDevicePropertiesPath(),
                    state->audioDevices->getDeviceSelection());
            }
            layoutComponents();
            if (window)
            {
                window->renderFrameIfDirty();
            }
        });
    outputDeviceDropdown->setOnSelectionChanged(
        [this](const int)
        {
            if (syncSelectionToAudioDevices())
            {
                persistence::AudioDevicePropertiesPersistence::save(
                    state->paths->audioDevicePropertiesPath(),
                    state->audioDevices->getDeviceSelection());
            }
        });
    inputDeviceDropdown->setOnSelectionChanged(
        [this](const int)
        {
            if (syncSelectionToAudioDevices())
            {
                persistence::AudioDevicePropertiesPersistence::save(
                    state->paths->audioDevicePropertiesPath(),
                    state->audioDevices->getDeviceSelection());
            }
        });
}

DevicePropertiesPane::~DevicePropertiesPane()
{
    if (ownsPortAudio)
    {
        Pa_Terminate();
    }
}

void DevicePropertiesPane::resized()
{
    layoutComponents();
}

void DevicePropertiesPane::populateHostApis()
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

void DevicePropertiesPane::populateDevices(
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
    int inputSelectionIndex = findIndex(inputDeviceIndices,
                                        preferredInputDeviceIndex);
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

int DevicePropertiesPane::getSelectedHostApiIndex() const
{
    if (!deviceTypeDropdown)
    {
        return -1;
    }
    const int index = deviceTypeDropdown->getSelectedIndex();
    if (index < 0 || index >= static_cast<int>(hostApiIndices.size()))
    {
        return -1;
    }
    return hostApiIndices[static_cast<std::size_t>(index)];
}

int DevicePropertiesPane::getSelectedDeviceIndex(
    const DropdownMenu *dropdown, const std::vector<int> &indices) const
{
    if (!dropdown)
    {
        return -1;
    }
    const int index = dropdown->getSelectedIndex();
    if (index < 0 || index >= static_cast<int>(indices.size()))
    {
        return -1;
    }
    return indices[static_cast<std::size_t>(index)];
}

bool DevicePropertiesPane::syncSelectionToAudioDevices()
{
    if (!state || !state->audioDevices)
    {
        return false;
    }

    const audio::AudioDevices::DeviceSelection selection{
        .hostApiIndex = getSelectedHostApiIndex(),
        .outputDeviceIndex =
            getSelectedDeviceIndex(outputDeviceDropdown, outputDeviceIndices),
        .inputDeviceIndex =
            getSelectedDeviceIndex(inputDeviceDropdown, inputDeviceIndices)};
    return state->audioDevices->setDeviceSelection(selection);
}

void DevicePropertiesPane::layoutComponents() const
{
    const int canvasWi = getWidth();
    const int canvasHi = getHeight();

    background->setBounds(0, 0, canvasWi, canvasHi);

    const int padding = scaleUi(state, 8.0f);
    const uint8_t labelFontPointSize =
        scaleFontPointSize(state, state->menuFontSize);
    const auto [deviceTypeLabelW, deviceTypeLabelH] =
        measureText("Device Type", labelFontPointSize);
    const auto [outputDeviceLabelW, outputDeviceLabelH] =
        measureText("Output Device", labelFontPointSize);
    const auto [inputDeviceLabelW, inputDeviceLabelH] =
        measureText("Input Device", labelFontPointSize);
    const int labelWidth =
        std::max({std::max(1, static_cast<int>(std::ceil(deviceTypeLabelW))),
                  std::max(1, static_cast<int>(std::ceil(outputDeviceLabelW))),
                  std::max(1, static_cast<int>(std::ceil(inputDeviceLabelW)))}) +
        padding;
    const int rowHeight =
        std::max({std::max(1, static_cast<int>(std::ceil(deviceTypeLabelH))),
                  std::max(1, static_cast<int>(std::ceil(outputDeviceLabelH))),
                  std::max(1, static_cast<int>(std::ceil(inputDeviceLabelH)))}) +
        padding * 2;
    const int dropdownX = padding + labelWidth + padding;
    const int dropdownW = std::max(1, canvasWi - dropdownX - padding);

    deviceTypeLabel->setBounds(padding, padding, labelWidth, rowHeight);
    deviceTypeDropdown->setBounds(dropdownX, padding, dropdownW, rowHeight);
    deviceTypeDropdown->setCollapsedHeight(rowHeight);
    deviceTypeDropdown->setItemMargin(padding);

    const int secondRowY = padding + rowHeight + padding;
    outputDeviceLabel->setBounds(padding, secondRowY, labelWidth, rowHeight);
    outputDeviceDropdown->setBounds(dropdownX, secondRowY, dropdownW, rowHeight);
    outputDeviceDropdown->setCollapsedHeight(rowHeight);
    outputDeviceDropdown->setItemMargin(padding);

    const int thirdRowY = secondRowY + rowHeight + padding;
    inputDeviceLabel->setBounds(padding, thirdRowY, labelWidth, rowHeight);
    inputDeviceDropdown->setBounds(dropdownX, thirdRowY, dropdownW, rowHeight);
    inputDeviceDropdown->setCollapsedHeight(rowHeight);
    inputDeviceDropdown->setItemMargin(padding);
}
