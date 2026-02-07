#pragma once

#include "Colors.hpp"
#include "Component.hpp"

#include <functional>

namespace cupuacu::gui
{
    class ScrollBar : public Component
    {
    public:
        enum class Orientation
        {
            Horizontal,
            Vertical
        };

        using GetterFn = std::function<double()>;
        using SetterFn = std::function<void(double)>;

        ScrollBar(State *state, Orientation orientation, GetterFn getValue,
                  GetterFn getMinValue, GetterFn getMaxValue,
                  GetterFn getPageSize, SetterFn setValue);

        void onDraw(SDL_Renderer *renderer) override;
        bool mouseDown(const MouseEvent &event) override;
        bool mouseMove(const MouseEvent &event) override;
        bool mouseUp(const MouseEvent &event) override;
        void timerCallback() override;

    private:
        struct Metrics
        {
            double minValue = 0.0;
            double maxValue = 0.0;
            double pageSize = 1.0;
            double value = 0.0;
            double range = 0.0;
            double total = 1.0;
            double trackLength = 0.0;
            double thumbLength = 0.0;
            double thumbPos = 0.0;
            bool valid = false;
        };

        Orientation orientation;
        GetterFn getValue;
        GetterFn getMinValue;
        GetterFn getMaxValue;
        GetterFn getPageSize;
        SetterFn setValue;

        bool dragging = false;
        double dragGrabOffset = 0.0;

        double lastValue = -1.0;
        double lastMinValue = -1.0;
        double lastMaxValue = -1.0;
        double lastPageSize = -1.0;
        int lastLength = -1;

        Metrics computeMetrics() const;
        double primaryAxisLength() const;
        double primaryAxisCoordinate(const MouseEvent &event) const;
        SDL_FRect thumbRect(const Metrics &metrics) const;
        bool pointInThumb(const MouseEvent &event, const Metrics &metrics) const;
        void setFromThumbPosition(double thumbStart, const Metrics &metrics);
    };
} // namespace cupuacu::gui

