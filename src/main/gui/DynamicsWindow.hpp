#pragma once

#include "DropdownMenu.hpp"
#include "Label.hpp"
#include "OpaqueRect.hpp"
#include "Slider.hpp"
#include "TextButton.hpp"
#include "TextInput.hpp"
#include "Window.hpp"

#include <memory>
#include <string>

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
        Window *getWindow() const
        {
            return window.get();
        }

    private:
        State *state = nullptr;
        std::unique_ptr<Window> window;
        OpaqueRect *background = nullptr;
        Label *thresholdLabel = nullptr;
        Label *ratioLabel = nullptr;
        TextInput *thresholdInput = nullptr;
        Slider *thresholdSlider = nullptr;
        DropdownMenu *ratioDropdown = nullptr;
        TextButton *resetButton = nullptr;
        TextButton *cancelButton = nullptr;
        TextButton *applyButton = nullptr;
        double thresholdPercent = 100.0;

        static constexpr double kMinPercent = 0.0;
        static constexpr double kMaxPercent = 100.0;

        void setThresholdPercent(double value, bool refreshInput = true);
        void syncSettings() const;
        void syncInputs();
        void bindControls();
        void applyEffect();
        void setDefaults();
        void layoutComponents() const;
        void closeNow();

        static std::string formatPercent(double value);
        static bool tryParsePercent(const std::string &text, double &valueOut);
    };
} // namespace cupuacu::gui
