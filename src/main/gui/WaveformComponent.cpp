#include "WaveformComponent.h"
#include <cmath>
#include <algorithm>
#include "smooth_line.h"

WaveformComponent::WaveformComponent(CupuacuState *state) : Component(state)
{
}

void WaveformComponent::updateSamplePoints()
{
    removeAllChildren();

    if (shouldShowSamplePoints(state->samplesPerPixel, state->hardwarePixelsPerAppPixel))
    {
        auto samplePoints = computeSamplePoints(
                getWidth(),
                getHeight(),
                state->sampleDataL,
                state->sampleOffset,
                state->samplesPerPixel,
                state->verticalZoom,
                state->hardwarePixelsPerAppPixel);

        for (auto &sp : samplePoints)
        {
            addChildAndSetDirty(sp);
        }
    }
}

bool WaveformComponent::shouldShowSamplePoints(const double samplesPerPixel, const uint8_t hardwarePixelsPerAppPixel)
{
    return samplesPerPixel < ((float)hardwarePixelsPerAppPixel / 40.f);
}

int getYPosForSampleValue(const int16_t sampleValue, const uint16_t waveformHeight, const double verticalZoom)
{
    return (waveformHeight / 2.0f) - ((sampleValue * verticalZoom * (waveformHeight / 2.0f)) / 32768.0f);
}

std::vector<std::unique_ptr<SamplePoint>> WaveformComponent::computeSamplePoints(int width, int height,
                                 const std::vector<int16_t>& samples, size_t offset,
                                 float samplesPerPixel, float verticalZoom, const uint8_t hardwarePixelsPerAppPixel)
{
    const int neededInputSamples = static_cast<int>(std::ceil((width + 1) * samplesPerPixel));
    const int availableSamples = static_cast<int>(samples.size()) - static_cast<int>(offset);
    const int actualInputSamples = std::min(neededInputSamples, availableSamples);

    if (actualInputSamples < 4)
        return {};

    std::vector<double> x(actualInputSamples);

    for (int i = 0; i < actualInputSamples; ++i)
    {
        x[i] = i / samplesPerPixel;
    }

    std::vector<std::unique_ptr<SamplePoint>> result;

    const auto samplePointSize = 32 / hardwarePixelsPerAppPixel;

    for (int i = 0; i < actualInputSamples; ++i)
    {
        int xPos = x[i];
        int yPos = getYPosForSampleValue(samples[offset + i], height, verticalZoom);

        auto samplePoint = std::make_unique<SamplePoint>(state, offset + i);
        samplePoint->setBounds(xPos - (samplePointSize / 2), yPos - (samplePointSize / 2), samplePointSize, samplePointSize);
        result.push_back(std::move(samplePoint));
    }

    return result;
}

void WaveformComponent::renderSmoothWaveform(SDL_Renderer* renderer, int width, int height,
                                 const std::vector<int16_t>& samples, size_t offset,
                                 float samplesPerPixel, float verticalZoom, const uint8_t hardwarePixelsPerAppPixel)
{
    const int neededInputSamples = static_cast<int>(std::ceil((width + 1) * samplesPerPixel));
    const int availableSamples = static_cast<int>(samples.size()) - static_cast<int>(offset);
    const int actualInputSamples = std::min(neededInputSamples, availableSamples);

    if (actualInputSamples < 4)
        return;

    std::vector<double> x(actualInputSamples);
    std::vector<double> y(actualInputSamples);

    for (int i = 0; i < actualInputSamples; ++i)
    {
        x[i] = i / samplesPerPixel;
        y[i] = static_cast<double>(samples[offset + i]);
    }

    int numPoints = width + 1;
    std::vector<double> xq(numPoints);
    for (int i = 0; i < numPoints; ++i)
        xq[i] = static_cast<double>(i);

    auto smoothened = splineInterpolateNonUniform(x, y, xq);

    for (int i = 0; i < numPoints - 1; ++i)
    {
        float x1 = static_cast<float>(xq[i]);
        float x2 = static_cast<float>(xq[i + 1]);

        float y1f = height / 2.0f - (smoothened[i] * verticalZoom * height / 2.0f) / 32768.0f;
        float y2f = height / 2.0f - (smoothened[i + 1] * verticalZoom * height / 2.0f) / 32768.0f;

        float thickness = 1.0f;

        float dx = x2 - x1;
        float dy = y2f - y1f;
        float len = std::sqrt(dx*dx + dy*dy);

        if (len == 0.0f)
            continue;

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
            verts[j].color = { 0, 0.5, 0, 1.f };
            verts[j].tex_coord = { 0, 0 };
        }

        int indices[6] = { 0, 1, 2, 0, 2, 3 };
        SDL_RenderGeometry(renderer, nullptr, verts, 4, indices, 6);
    }
}

static void renderBlockWaveform(SDL_Renderer* renderer, int width, int height,
                                const std::vector<int16_t>& samples, size_t offset,
                                float samplesPerPixel, float verticalZoom)
{
    const float scale = (verticalZoom * height * 0.5f) / 32768.0f;

    int prevY = 0;
    bool hasPrev = false;
    bool noMoreStuffToDraw = false;

    for (int x = 0; x < width; ++x)
    {
        if (noMoreStuffToDraw)
            break;

        const size_t startSample = static_cast<size_t>(x * samplesPerPixel) + offset;
        size_t endSample = static_cast<size_t>((x + 1) * samplesPerPixel) + offset;

        if (endSample == startSample)
            endSample++;

        int16_t minSample = INT16_MAX;
        int16_t maxSample = INT16_MIN;

        for (size_t i = startSample; i < endSample; ++i)
        {
            if (i >= samples.size())
            {
                noMoreStuffToDraw = true;
                break;
            }

            const int16_t s = samples[i];
            minSample = std::min(minSample, s);
            maxSample = std::max(maxSample, s);
        }

        const float midSample = (minSample + maxSample) * 0.5f;
        const int y = static_cast<int>(float(height) / 2 - midSample * scale);

        if (hasPrev)
        {
            if ((y >= 0 || prevY >= 0) && (y < height || prevY < height))
            {
                SDL_RenderLine(renderer, x - 1, prevY, x, y);
            }
        }

        if (startSample >= samples.size())
            break;

        prevY = y;
        hasPrev = true;

        int y1 = static_cast<int>(float(height) / 2 - maxSample * scale);
        int y2 = static_cast<int>(float(height) / 2 - minSample * scale);

        if ((y1 < 0 && y2 < 0) || (y1 >= height && y2 >= height))
            continue;

        if (y1 != y2)
        {
            SDL_RenderLine(renderer, x, y1, x, y2);
        }
        else
        {
            SDL_RenderPoint(renderer, x, y1);
        }
    }
}

void WaveformComponent::onDraw(SDL_Renderer *renderer)
{
    const float samplesPerPixel = state->samplesPerPixel;
    const float verticalZoom = state->verticalZoom;
    const size_t sampleOffset = state->sampleOffset;
    const auto& sampleDataL = state->sampleDataL;
    const auto selectionStart = state->selectionStartSample;
    const auto selectionEnd = state->selectionEndSample;

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderFillRect(renderer, NULL);
    SDL_SetRenderDrawColor(renderer, 0, 185, 0, 255);

    if (samplesPerPixel < 1)
    {
        renderSmoothWaveform(
                renderer,
                getWidth(),
                getHeight(),
                sampleDataL,
                sampleOffset,
                samplesPerPixel,
                verticalZoom,
                state->hardwarePixelsPerAppPixel);
    }
    else
    {
        renderBlockWaveform(
                renderer, 
                getWidth(),
                getHeight(),
                sampleDataL,
                sampleOffset,
                samplesPerPixel,
                verticalZoom);
    }

    if (selectionStart != selectionEnd && selectionEnd >= sampleOffset)
    {
        const bool goesRight = selectionEnd > selectionStart;

        float orderedStart = goesRight ? selectionStart : selectionEnd;
        float orderedEnd = goesRight ? selectionEnd : selectionStart;

        double firstSample = std::floor(orderedStart);
        double lastSample = std::floor(orderedEnd);

        if (firstSample >= lastSample) return;

        if (shouldShowSamplePoints(samplesPerPixel, state->hardwarePixelsPerAppPixel))
        {
            firstSample -= 0.5f;
            lastSample -= 0.5f;
        }
 
        float startX = firstSample <= sampleOffset ? 0 : (firstSample - sampleOffset) / samplesPerPixel;
        float endX = (lastSample - sampleOffset) / samplesPerPixel;

        SDL_SetRenderDrawColor(renderer, 0, 64, 255, 128);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        auto selectionWidth = std::abs(endX - startX) < 1 ? 1 : endX - startX;

        if (endX - startX < 0 && selectionWidth > 0) selectionWidth = -selectionWidth;

        SDL_FRect selectionRect = {
            startX,
            0.0f,
            selectionWidth,
            (float)getHeight()
        };

        SDL_RenderFillRect(renderer, &selectionRect);
    }
}

void WaveformComponent::timerCallback()
{
    if (state->samplesToScroll != 0.0f)
    {
        const auto scroll = state->samplesToScroll;
        const uint64_t oldOffset = state->sampleOffset;

        if (scroll < 0)
        {
            auto absScroll = -scroll;

            state->sampleOffset = (state->sampleOffset > absScroll)
                ? state->sampleOffset - absScroll
                : 0;

            state->selectionEndSample = (state->selectionEndSample > absScroll)
                ? state->selectionEndSample - absScroll
                : 0;
        }
        else
        {
            state->sampleOffset += scroll;
            state->selectionEndSample += scroll;
        }

        if (oldOffset != state->sampleOffset)
        {
            setDirty();
            updateSamplePoints();
        }
    }
}

void WaveformComponent::handleScroll(const SDL_Event &event)
{
    const auto samplesPerPixel = state->samplesPerPixel;
    auto sampleOffset = state->sampleOffset;
    auto motionx = event.motion.x;

    if (event.motion.state & SDL_BUTTON_LMASK)
    {
        if (motionx > getWidth() || motionx < 0)
        {
            auto diff = (motionx < 0)
                ? motionx
                : motionx - getWidth();

            auto samplesToScroll = diff * samplesPerPixel;

            if (samplesToScroll < 0)
            {
                double absScroll = -samplesToScroll;
                state->sampleOffset = (state->sampleOffset > absScroll)
                    ? state->sampleOffset - absScroll
                    : 0;
            }
            else
            {
                state->sampleOffset += samplesToScroll;
            }

            sampleOffset = state->sampleOffset;
            state->samplesToScroll = samplesToScroll;
        }
        else
        {
            state->samplesToScroll = 0;
        }

        if (samplesPerPixel < 1.f)
        {
            motionx += 0.5f/samplesPerPixel;
        }

        const float x = motionx <= 0 ? 0.f : motionx;
        state->selectionEndSample = sampleOffset + (x * samplesPerPixel);

        setDirty();
        updateSamplePoints();
    }
}

void WaveformComponent::handleDoubleClick()
{
    state->selectionStartSample = 0;
    state->selectionEndSample = state->sampleDataL.size();
    setDirty();
    updateSamplePoints();
}

void WaveformComponent::startSelection(const SDL_Event &e)
{
    const auto samplesPerPixel = state->samplesPerPixel;
    const auto sampleOffset = state->sampleOffset;

    auto buttonx = e.button.x;

    if (samplesPerPixel < 1)
    {
        buttonx += 0.5f / samplesPerPixel;
    }

    state->selectionStartSample = sampleOffset + (buttonx * samplesPerPixel);
    state->selectionEndSample = state->selectionStartSample;
}

void WaveformComponent::endSelection(const SDL_Event &e)
{
    const auto samplesPerPixel = state->samplesPerPixel;

    auto buttonx = e.button.x;

    if (samplesPerPixel < 1)
    {
        buttonx += 0.5f / samplesPerPixel;
    }

    state->selectionEndSample = state->sampleOffset + (buttonx * state->samplesPerPixel);

    if (state->selectionEndSample < state->selectionStartSample)
    {
        auto temp = state->selectionStartSample;
        state->selectionStartSample = state->selectionEndSample;
        state->selectionEndSample = temp;
    }

    state->samplesToScroll = 0;
    setDirty();
    updateSamplePoints();
}

bool WaveformComponent::onHandleEvent(const SDL_Event &event)
{
    switch (event.type)
    {
        case SDL_EVENT_MOUSE_MOTION:
        {
            handleScroll(event);
            break;
        }
        case SDL_EVENT_MOUSE_WHEEL:
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        {
            if (event.button.button == SDL_BUTTON_LEFT)
            {
                if (event.button.clicks >= 2)
                {
                    handleDoubleClick();
                    break;
                }

                startSelection(event);
            }
            break;
        }
        case SDL_EVENT_MOUSE_BUTTON_UP:
        {
            if (event.button.button == SDL_BUTTON_LEFT)
            {
                if (event.button.clicks >= 2)
                {
                    break;
                }

                endSelection(event);
            }
            break;
        }
        default:
            break;
    }

    return false;
}

