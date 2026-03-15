#pragma once

#include "Component.hpp"

#include <functional>

namespace cupuacu::gui
{
    class Slider : public Component
    {
    public:
        using GetterFn = std::function<double()>;
        using SetterFn = std::function<void(double)>;

        Slider(State *stateToUse, GetterFn getValueToUse,
               GetterFn getMinValueToUse, GetterFn getMaxValueToUse,
               SetterFn setValueToUse);

        bool mouseDown(const MouseEvent &event) override;
        bool mouseMove(const MouseEvent &event) override;
        bool mouseUp(const MouseEvent &event) override;
        void onDraw(SDL_Renderer *renderer) override;
        void timerCallback() override;

    private:
        GetterFn getValue;
        GetterFn getMinValue;
        GetterFn getMaxValue;
        SetterFn setValue;

        bool dragging = false;
        double lastValue = 0.0;
        double lastMinValue = 0.0;
        double lastMaxValue = 0.0;
        int lastWidth = 0;

        double normalizedValue() const;
        void setFromMouseX(float x);
    };
} // namespace cupuacu::gui
