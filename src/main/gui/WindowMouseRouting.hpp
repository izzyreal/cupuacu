#pragma once

#include "MouseEvent.hpp"

namespace cupuacu::gui
{
    struct WindowMouseRoutingPlan
    {
        bool handled = false;
        bool dispatchToRoot = false;
        bool updateHoverBeforeDispatch = false;
        bool updateHoverAfterDispatch = false;
        bool sendLeaveToCaptureBeforeDispatch = false;
        bool clearCaptureAfterDispatch = false;
    };

    inline WindowMouseRoutingPlan planWindowMouseRouting(
        const MouseEventType type, const bool hasRootComponent,
        const bool hasCapturingComponent, const bool captureContainsPoint)
    {
        WindowMouseRoutingPlan plan{};

        if (!hasRootComponent)
        {
            return plan;
        }

        plan.handled = true;
        plan.dispatchToRoot = true;

        switch (type)
        {
            case MOVE:
                plan.updateHoverAfterDispatch = !hasCapturingComponent;
                break;
            case DOWN:
                plan.updateHoverBeforeDispatch = !hasCapturingComponent;
                break;
            case UP:
                plan.updateHoverBeforeDispatch = true;
                plan.sendLeaveToCaptureBeforeDispatch =
                    hasCapturingComponent && !captureContainsPoint;
                plan.clearCaptureAfterDispatch = hasCapturingComponent;
                break;
            case WHEEL:
                plan.updateHoverBeforeDispatch = true;
                break;
        }

        return plan;
    }
} // namespace cupuacu::gui
