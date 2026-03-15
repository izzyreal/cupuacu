#include "Slider.hpp"

#include "Colors.hpp"
#include "Helpers.hpp"
#include "UiScale.hpp"

#include <algorithm>
#include <cmath>

namespace
{
    constexpr SDL_Color kTrackColor{76, 76, 76, 255};
    constexpr SDL_Color kTrackFillColor{82, 114, 164, 255};
    constexpr SDL_Color kThumbColor{188, 188, 188, 255};
    constexpr SDL_Color kThumbDraggingColor{220, 220, 220, 255};
}

using namespace cupuacu::gui;

Slider::Slider(State *stateToUse, GetterFn getValueToUse,
               GetterFn getMinValueToUse, GetterFn getMaxValueToUse,
               SetterFn setValueToUse)
    : Component(stateToUse, "Slider"), getValue(std::move(getValueToUse)),
      getMinValue(std::move(getMinValueToUse)),
      getMaxValue(std::move(getMaxValueToUse)),
      setValue(std::move(setValueToUse))
{
}

bool Slider::mouseDown(const MouseEvent &event)
{
    if (event.mouseXi < 0 || event.mouseYi < 0 || event.mouseXi >= getWidth() ||
        event.mouseYi >= getHeight())
    {
        return false;
    }

    dragging = true;
    setFromMouseX(event.mouseXf);
    setDirty();
    return true;
}

bool Slider::mouseMove(const MouseEvent &event)
{
    if (!dragging || !event.buttonState.left)
    {
        return false;
    }

    setFromMouseX(event.mouseXf);
    setDirty();
    return true;
}

bool Slider::mouseUp(const MouseEvent &event)
{
    if (!dragging)
    {
        return false;
    }

    dragging = false;
    setFromMouseX(event.mouseXf);
    setDirty();
    return true;
}

void Slider::onDraw(SDL_Renderer *renderer)
{
    const SDL_Rect bounds = getLocalBounds();
    Helpers::fillRect(renderer, bounds, Colors::background);

    const int thumbWidth = std::max(8, scaleUi(state, 12.0f));
    const int trackHeight = std::max(4, scaleUi(state, 6.0f));
    const int trackY = (bounds.h - trackHeight) / 2;

    Helpers::fillRect(renderer, SDL_Rect{0, trackY, bounds.w, trackHeight},
                      kTrackColor);

    const double normalized = normalizedValue();
    const int fillWidth =
        static_cast<int>(std::lround(normalized * std::max(0, bounds.w - thumbWidth))) +
        thumbWidth / 2;
    Helpers::fillRect(renderer, SDL_Rect{0, trackY, fillWidth, trackHeight},
                      kTrackFillColor);

    const int thumbX = static_cast<int>(std::lround(
        normalized * std::max(0, bounds.w - thumbWidth)));
    Helpers::fillRect(renderer, SDL_Rect{thumbX, 0, thumbWidth, bounds.h},
                      dragging ? kThumbDraggingColor : kThumbColor);
}

void Slider::timerCallback()
{
    const double currentValue = getValue ? getValue() : 0.0;
    const double currentMin = getMinValue ? getMinValue() : 0.0;
    const double currentMax = getMaxValue ? getMaxValue() : 1.0;
    if (currentValue != lastValue || currentMin != lastMinValue ||
        currentMax != lastMaxValue || getWidth() != lastWidth)
    {
        lastValue = currentValue;
        lastMinValue = currentMin;
        lastMaxValue = currentMax;
        lastWidth = getWidth();
        setDirty();
    }
}

double Slider::normalizedValue() const
{
    const double minValue = getMinValue ? getMinValue() : 0.0;
    const double maxValue = getMaxValue ? getMaxValue() : 1.0;
    const double value = getValue ? getValue() : minValue;
    if (maxValue <= minValue)
    {
        return 0.0;
    }
    return std::clamp((value - minValue) / (maxValue - minValue), 0.0, 1.0);
}

void Slider::setFromMouseX(const float x)
{
    const double minValue = getMinValue ? getMinValue() : 0.0;
    const double maxValue = getMaxValue ? getMaxValue() : 1.0;
    if (!setValue || getWidth() <= 1 || maxValue <= minValue)
    {
        return;
    }

    const double normalized =
        std::clamp(static_cast<double>(x) / std::max(1, getWidth() - 1), 0.0,
                   1.0);
    setValue(minValue + normalized * (maxValue - minValue));
}
