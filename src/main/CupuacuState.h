#pragma once

#include <cstdint>
#include <vector>
#include <string>

struct CupuacuState {
    uint8_t hardwarePixelsPerAppPixel = 16;
    std::string currentFile = "/Users/izmar/Downloads/ams_chill.wav";

    std::vector<int16_t> sampleDataL;
    std::vector<int16_t> sampleDataR;

    double samplesPerPixel = 1;
    double verticalZoom = 1;
    uint64_t sampleOffset = 0;
};

