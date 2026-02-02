#include "AudioDevices.hpp"

#include "AudioDevice.hpp"
#include "PaUtil.hpp"

#include <portaudio.h>

using namespace cupuacu;

AudioDevices::AudioDevices()
{
    PaError err = Pa_Initialize();

    if (err != paNoError)
    {
        PaUtil::handlePaError(err);
        return;
    }

    outputDevice = std::make_shared<AudioDevice>();
    outputDevice->openDevice();
}

AudioDevices::~AudioDevices()
{
    Pa_Terminate();
}

AudioDevicePtr AudioDevices::getOutputDevice()
{
    return outputDevice;
}
