#pragma once

#include <cstdlib>
#include <cstdio>
#include <portaudio.h>

namespace cupuacu
{
    class PaUtil
    {
    public:
        static void handlePaError(PaError &err)
        {
            const char *suppress = std::getenv("CUPUACU_SUPPRESS_PORTAUDIO_ERRORS");
            if (suppress != nullptr && suppress[0] != '\0' &&
                suppress[0] != '0')
            {
                return;
            }
            printf("An error occurred while using the portaudio stream\n");
            printf("Error number: %d\n", err);
            printf("Error message: %s\n", Pa_GetErrorText(err));
        }
    };
} // namespace cupuacu
