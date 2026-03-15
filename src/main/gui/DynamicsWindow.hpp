#pragma once

#include "Label.hpp"
#include "OpaqueRect.hpp"
#include "TextButton.hpp"
#include "Window.hpp"

#include <memory>

namespace cupuacu
{
    struct State;
}

namespace cupuacu::gui
{
    class DynamicsWindow
    {
    public:
        explicit DynamicsWindow(State *stateToUse);
        ~DynamicsWindow();

        bool isOpen() const
        {
            return window && window->isOpen();
        }
        void raise() const;

    private:
        State *state = nullptr;
        std::unique_ptr<Window> window;
        OpaqueRect *background = nullptr;
        Label *messageLabel = nullptr;
        TextButton *cancelButton = nullptr;
        TextButton *applyButton = nullptr;

        void layoutComponents() const;
        void closeNow();
    };
} // namespace cupuacu::gui
