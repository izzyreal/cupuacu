#include <SDL3/SDL.h>

#include "smooth_line.h"

#include <vector>

static void paintWaveformToCanvas(
        SDL_Renderer *renderer,
        SDL_Texture *canvas,
        const std::vector<int16_t> &sampleDataL,
        const double samplesPerPixel,
        const double verticalZoom,
        const int64_t sampleOffset)
{
    SDL_SetRenderTarget(renderer, canvas);

    SDL_SetRenderDrawColor(renderer, 0, samplesPerPixel < 1 ? 50 : 0, 0, 255);
    SDL_RenderClear(renderer);

    SDL_SetRenderDrawColor(renderer, 0, 255, 0, 255);

    int width, height;
    SDL_GetCurrentRenderOutputSize(renderer, &width, &height);

    if (samplesPerPixel < 1)
    {
        const float factor = 1.f / samplesPerPixel;

        const int neededInputSamples = std::ceil((width + 1) * samplesPerPixel);

        const int availableSamples = static_cast<int>(sampleDataL.size()) - sampleOffset;
        const int actualInputSamples = std::min(neededInputSamples, availableSamples);

        if (actualInputSamples < 3)
        {
            SDL_SetRenderTarget(renderer, NULL);
            return;
        }

        std::vector<int16_t> data(actualInputSamples);
        std::copy(sampleDataL.begin() + sampleOffset, sampleDataL.begin() + sampleOffset + actualInputSamples, data.begin());

        const int numDisplayPoints = std::min(width + 1, static_cast<int>(factor * actualInputSamples));
        const auto smoothened = smoothenCubic(data, numDisplayPoints);
        const float xSpacing = static_cast<float>(width) / (numDisplayPoints - 1);

        for (int i = 0; i < numDisplayPoints - 1; ++i)
        {
            float x1 = i * xSpacing;
            float x2 = (i + 1) * xSpacing;
            float y1f = height / 2.0f - (smoothened[i] * verticalZoom * height / 2.0f) / 32768.0f;
            float y2f = height / 2.0f - (smoothened[i + 1] * verticalZoom * height / 2.0f) / 32768.0f;
            float thickness = 1.0f;

            float dx = x2 - x1;
            float dy = y2f - y1f;
            float len = std::sqrt(dx*dx + dy*dy);

            if (len == 0.0f)
            {
                continue;
            }

            dx /= len;
            dy /= len;

            float px = -dy * thickness * 0.5f;
            float py = dx * thickness * 0.5f;

            SDL_Vertex verts[4];
            verts[0].position = { x1 - px, y1f - py };
            verts[1].position = { x1 + px, y1f + py };
            verts[2].position = { x2 + px, y2f + py };
            verts[3].position = { x2 - px, y2f - py };

            for (int j = 0; j < 4; ++j)
            {
                verts[j].color = { 0, 255, 0, 255 };
                verts[j].tex_coord = { 0, 0 };
            }

            int indices[6] = { 0, 1, 2, 0, 2, 3 };
            SDL_RenderGeometry(renderer, nullptr, verts, 4, indices, 6);
        }

        SDL_SetRenderTarget(renderer, NULL);
        return;
    }

    bool noMoreStuffToDraw = false;
    float scale = (verticalZoom * height * 0.5f) / 32768.0f;

    int prevY = 0;
    bool hasPrev = false;

    for (int x = 0; x < width; ++x)
    {
        if (noMoreStuffToDraw)
        {
            break;
        }

        const size_t startSample = static_cast<size_t>(x * samplesPerPixel) + sampleOffset;
        size_t endSample   = static_cast<size_t>((x + 1) * samplesPerPixel) + sampleOffset;

        if (endSample == startSample)
        {
            endSample++;
        }

        int16_t minSample = INT16_MAX;
        int16_t maxSample = INT16_MIN;

        for (size_t i = startSample; i < endSample; ++i)
        {
            if (i >= sampleDataL.size())
            {
                noMoreStuffToDraw = true;
                break;
            }

            const int16_t s = sampleDataL[i];

            if (s < minSample)
            {
                minSample = s;
            }

            if (s > maxSample)
            {
                maxSample = s;
            }
        }

        const float midSample = (minSample + maxSample) * 0.5f;

        int y = static_cast<int>(float(height) / 2 - midSample * scale);
        y = std::clamp(y, 0, height - 1);

        if (hasPrev)
        {
            SDL_RenderLine(renderer, x - 1, prevY, x, y);
        }

        if (startSample >= sampleDataL.size())
        {
            break;
        }

        prevY = y;
        hasPrev = true;

        int y1 = static_cast<int>(float(height) / 2 - maxSample * scale);
        int y2 = static_cast<int>(float(height) / 2 - minSample * scale);
        y1 = std::clamp(y1, 0, height - 1);
        y2 = std::clamp(y2, 0, height - 1);
        
        if (y1 != y2)
        {
            SDL_RenderLine(renderer, x, y1, x, y2);
        }
        else
        {
            SDL_RenderPoint(renderer, x, y1);
        }
    }

    SDL_SetRenderTarget(renderer, NULL);
}

