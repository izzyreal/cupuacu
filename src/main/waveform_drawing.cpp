#include "waveform_drawing.h"

#include "CupuacuState.h"

#include "smooth_line.h"

void paintWaveformToCanvas(
        SDL_Renderer *renderer,
        SDL_Texture *canvas,
        CupuacuState *state)
{
    SDL_SetRenderTarget(renderer, canvas);

    SDL_SetRenderDrawColor(renderer, 0, state->samplesPerPixel < 1 ? 50 : 0, 0, 255);
    SDL_RenderClear(renderer);

    SDL_SetRenderDrawColor(renderer, 0, 255, 0, 255);

    int width, height;
    SDL_GetCurrentRenderOutputSize(renderer, &width, &height);

    if (state->samplesPerPixel < 1)
    {
        const float factor = 1.f / state->samplesPerPixel;

        const int neededInputSamples = std::ceil((width + 1) * state->samplesPerPixel);

        const int availableSamples = static_cast<int>(state->sampleDataL.size()) - state->sampleOffset;
        const int actualInputSamples = std::min(neededInputSamples, availableSamples);

        if (actualInputSamples < 3)
        {
            SDL_SetRenderTarget(renderer, NULL);
            return;
        }

        std::vector<int16_t> data(actualInputSamples);
        std::copy(state->sampleDataL.begin() + state->sampleOffset, state->sampleDataL.begin() + state->sampleOffset + actualInputSamples, data.begin());

        const int numDisplayPoints = std::min(width + 1, static_cast<int>(factor * actualInputSamples));
        const auto smoothened = smoothenCubic(data, numDisplayPoints);
        const float xSpacing = static_cast<float>(width) / (numDisplayPoints - 1);

        for (int i = 0; i < numDisplayPoints - 1; ++i)
        {
            float x1 = i * xSpacing;
            float x2 = (i + 1) * xSpacing;
            float y1f = height / 2.0f - (smoothened[i] * state->verticalZoom * height / 2.0f) / 32768.0f;
            float y2f = height / 2.0f - (smoothened[i + 1] * state->verticalZoom * height / 2.0f) / 32768.0f;
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
    float scale = (state->verticalZoom * height * 0.5f) / 32768.0f;

    int prevY = 0;
    bool hasPrev = false;

    for (int x = 0; x < width; ++x)
    {
        if (noMoreStuffToDraw)
        {
            break;
        }

        const size_t startSample = static_cast<size_t>(x * state->samplesPerPixel) + state->sampleOffset;
        size_t endSample   = static_cast<size_t>((x + 1) * state->samplesPerPixel) + state->sampleOffset;

        if (endSample == startSample)
        {
            endSample++;
        }

        int16_t minSample = INT16_MAX;
        int16_t maxSample = INT16_MIN;

        for (size_t i = startSample; i < endSample; ++i)
        {
            if (i >= state->sampleDataL.size())
            {
                noMoreStuffToDraw = true;
                break;
            }

            const int16_t s = state->sampleDataL[i];

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

        if (hasPrev)
        {
            if ((y >= 0 || prevY >= 0) && (y < height || prevY < height))
            {
                SDL_RenderLine(renderer, x - 1, prevY, x, y);
            }
        }

        if (startSample >= state->sampleDataL.size())
        {
            break;
        }

        prevY = y;
        hasPrev = true;

        int y1 = static_cast<int>(float(height) / 2 - maxSample * scale);
        int y2 = static_cast<int>(float(height) / 2 - minSample * scale);

        if ((y1 < 0 && y2 < 0) || (y1 >= height && y2 >= height)) {
            continue;
        }        

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

