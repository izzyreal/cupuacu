#include "audio/AudioDevices.hpp"

#include "audio/AudioDevice.hpp"
#include "PaUtil.hpp"

#include <portaudio.h>

using namespace cupuacu::audio;

AudioDevices::AudioDevices()
{
    PaError err = Pa_Initialize();

    if (err != paNoError)
    {
        cupuacu::PaUtil::handlePaError(err);
        return;
    }

    outputDevice = std::make_shared<AudioDevice>();

    DeviceSelection initialSelection{};
    const PaDeviceIndex defaultOutput = Pa_GetDefaultOutputDevice();
    if (defaultOutput != paNoDevice)
    {
        initialSelection.outputDeviceIndex = defaultOutput;
        const PaDeviceInfo *outputInfo = Pa_GetDeviceInfo(defaultOutput);
        if (outputInfo)
        {
            initialSelection.hostApiIndex = outputInfo->hostApi;
        }
    }

    const PaDeviceIndex defaultInput = Pa_GetDefaultInputDevice();
    if (defaultInput != paNoDevice)
    {
        initialSelection.inputDeviceIndex = defaultInput;
        if (initialSelection.hostApiIndex < 0)
        {
            const PaDeviceInfo *inputInfo = Pa_GetDeviceInfo(defaultInput);
            if (inputInfo)
            {
                initialSelection.hostApiIndex = inputInfo->hostApi;
            }
        }
    }

    {
        std::lock_guard<std::mutex> lock(selectionMutex);
        deviceSelection = initialSelection;
    }

    if (initialSelection.outputDeviceIndex >= 0)
    {
        outputDevice->openDevice(initialSelection.inputDeviceIndex,
                                 initialSelection.outputDeviceIndex);
    }
}

AudioDevices::~AudioDevices()
{
    Pa_Terminate();
}

AudioDevicePtr AudioDevices::getOutputDevice()
{
    return outputDevice;
}

AudioDevices::DeviceSelection AudioDevices::getDeviceSelection() const
{
    std::lock_guard<std::mutex> lock(selectionMutex);
    return deviceSelection;
}

bool AudioDevices::setDeviceSelection(const DeviceSelection &selection)
{
    {
        std::lock_guard<std::mutex> lock(selectionMutex);
        if (selection == deviceSelection)
        {
            return false;
        }
        deviceSelection = selection;
    }

    if (outputDevice)
    {
        outputDevice->openDevice(selection.inputDeviceIndex,
                                 selection.outputDeviceIndex);
    }

    return true;
}
