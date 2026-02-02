#pragma once

#include <cstdio>
#include <portaudio.h>

namespace cupuacu
{
    class PaUtil
    {
    public:
        static void handlePaError(PaError &err)
        {
            printf("An error occurred while using the portaudio stream\n");
            printf("Error number: %d\n", err);
            printf("Error message: %s\n", Pa_GetErrorText(err));
        }
    };
} // namespace cupuacu
