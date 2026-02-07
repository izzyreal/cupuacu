#include "ScrollBar.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>

using namespace cupuacu::gui;

ScrollBar::ScrollBar(State *state, const Orientation orientationToUse,
                     GetterFn getValueToUse, GetterFn getMinValueToUse,
                     GetterFn getMaxValueToUse, GetterFn getPageSizeToUse,
                     SetterFn setValueToUse)
    : Component(state, "ScrollBar"), orientation(orientationToUse),
      getValue(std::move(getValueToUse)),
      getMinValue(std::move(getMinValueToUse)),
      getMaxValue(std::move(getMaxValueToUse)),
      getPageSize(std::move(getPageSizeToUse)),
      setValue(std::move(setValueToUse))
{
}

double ScrollBar::primaryAxisLength() const
{
    return orientation == Orientation::Horizontal ? getWidth() : getHeight();
}

double ScrollBar::primaryAxisCoordinate(const MouseEvent &event) const
{
    return orientation == Orientation::Horizontal ? event.mouseXf : event.mouseYf;
}

ScrollBar::Metrics ScrollBar::computeMetrics() const
{
    Metrics m{};
    m.minValue = getMinValue ? getMinValue() : 0.0;
    m.maxValue = getMaxValue ? getMaxValue() : 0.0;
    m.pageSize = getPageSize ? std::max(1.0, getPageSize()) : 1.0;
    m.value = getValue ? getValue() : 0.0;
    m.trackLength = std::max(0.0, primaryAxisLength());

    if (m.maxValue < m.minValue)
    {
        std::swap(m.maxValue, m.minValue);
    }
    m.range = std::max(0.0, m.maxValue - m.minValue);
    m.total = m.pageSize + m.range;

    if (m.trackLength <= 0.0 || m.total <= 0.0)
    {
        return m;
    }

    const double minThumbPx = std::max(8.0, m.trackLength * 0.08);
    const double desiredThumb = m.trackLength * (m.pageSize / m.total);
    m.thumbLength = std::clamp(desiredThumb, minThumbPx, m.trackLength);

    if (m.range <= 0.0 || m.trackLength <= m.thumbLength)
    {
        m.thumbPos = 0.0;
    }
    else
    {
        const double norm =
            std::clamp((m.value - m.minValue) / m.range, 0.0, 1.0);
        m.thumbPos = norm * (m.trackLength - m.thumbLength);
    }

    m.valid = true;
    return m;
}

SDL_FRect ScrollBar::thumbRect(const Metrics &metrics) const
{
    if (orientation == Orientation::Horizontal)
    {
        return SDL_FRect{static_cast<float>(metrics.thumbPos), 0.0f,
                         static_cast<float>(metrics.thumbLength),
                         static_cast<float>(getHeight())};
    }
    return SDL_FRect{0.0f, static_cast<float>(metrics.thumbPos),
                     static_cast<float>(getWidth()),
                     static_cast<float>(metrics.thumbLength)};
}

bool ScrollBar::pointInThumb(const MouseEvent &event, const Metrics &metrics) const
{
    const SDL_FRect r = thumbRect(metrics);
    return event.mouseXf >= r.x && event.mouseXf <= r.x + r.w &&
           event.mouseYf >= r.y && event.mouseYf <= r.y + r.h;
}

void ScrollBar::setFromThumbPosition(const double thumbStart,
                                     const Metrics &metrics)
{
    if (!setValue || !metrics.valid || metrics.range <= 0.0)
    {
        return;
    }
    const double usable = metrics.trackLength - metrics.thumbLength;
    if (usable <= 0.0)
    {
        setValue(metrics.minValue);
        return;
    }

    const double clamped = std::clamp(thumbStart, 0.0, usable);
    const double norm = clamped / usable;
    const double next = metrics.minValue + norm * metrics.range;
    setValue(next);
}

void ScrollBar::onDraw(SDL_Renderer *renderer)
{
    const SDL_FRect bounds = getLocalBoundsF();
    SDL_SetRenderDrawColor(renderer, Colors::border.r, Colors::border.g,
                           Colors::border.b, Colors::border.a);
    SDL_RenderFillRect(renderer, &bounds);

    const Metrics metrics = computeMetrics();
    if (!metrics.valid)
    {
        return;
    }

    const SDL_FRect thumb = thumbRect(metrics);
    const SDL_Color thumbColor = dragging ? SDL_Color{145, 145, 145, 255}
                                          : SDL_Color{118, 118, 118, 255};
    SDL_SetRenderDrawColor(renderer, thumbColor.r, thumbColor.g, thumbColor.b,
                           thumbColor.a);
    SDL_RenderFillRect(renderer, &thumb);
}

bool ScrollBar::mouseDown(const MouseEvent &event)
{
    if (!event.buttonState.left)
    {
        return false;
    }

    const Metrics metrics = computeMetrics();
    if (!metrics.valid)
    {
        return true;
    }

    if (pointInThumb(event, metrics))
    {
        const SDL_FRect thumb = thumbRect(metrics);
        dragging = true;
        dragGrabOffset =
            primaryAxisCoordinate(event) -
            (orientation == Orientation::Horizontal ? thumb.x : thumb.y);
        return true;
    }

    dragging = true;
    dragGrabOffset = metrics.thumbLength * 0.5;
    const double thumbStart = primaryAxisCoordinate(event) - dragGrabOffset;
    setFromThumbPosition(thumbStart, metrics);
    return true;
}

bool ScrollBar::mouseMove(const MouseEvent &event)
{
    if (!dragging || !event.buttonState.left)
    {
        return false;
    }
    const Metrics metrics = computeMetrics();
    if (!metrics.valid)
    {
        return true;
    }

    const double thumbStart = primaryAxisCoordinate(event) - dragGrabOffset;
    setFromThumbPosition(thumbStart, metrics);
    return true;
}

bool ScrollBar::mouseUp(const MouseEvent &event)
{
    (void)event;
    if (!dragging)
    {
        return false;
    }
    dragging = false;
    setDirty();
    return true;
}

void ScrollBar::timerCallback()
{
    const Metrics metrics = computeMetrics();
    const int length = static_cast<int>(primaryAxisLength());
    if (metrics.value != lastValue || metrics.minValue != lastMinValue ||
        metrics.maxValue != lastMaxValue || metrics.pageSize != lastPageSize ||
        length != lastLength)
    {
        lastValue = metrics.value;
        lastMinValue = metrics.minValue;
        lastMaxValue = metrics.maxValue;
        lastPageSize = metrics.pageSize;
        lastLength = length;
        setDirty();
    }
}

