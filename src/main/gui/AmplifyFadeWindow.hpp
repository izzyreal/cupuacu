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
    class AmplifyFadeWindow
    {
    public:
        explicit AmplifyFadeWindow(State *stateToUse);
        ~AmplifyFadeWindow();

        bool isOpen() const
        {
            return window && window->isOpen();
        }
        void raise() const;
        Window *getWindow() const
        {
            return window.get();
        }

        double getStartPercent() const
        {
            return startPercent;
        }
        double getEndPercent() const
        {
            return endPercent;
        }
        int getCurveIndex() const
        {
            return curveDropdown ? curveDropdown->getSelectedIndex() : 0;
        }
        bool isLocked() const
        {
            return lockEnabled;
        }

    private:
        State *state = nullptr;
        std::unique_ptr<Window> window;

        OpaqueRect *background = nullptr;
        Label *startLabel = nullptr;
        Label *endLabel = nullptr;
        Label *curveLabel = nullptr;
        TextInput *startInput = nullptr;
        TextInput *endInput = nullptr;
        Slider *startSlider = nullptr;
        Slider *endSlider = nullptr;
        TextButton *lockButton = nullptr;
        DropdownMenu *curveDropdown = nullptr;
        TextButton *resetButton = nullptr;
        TextButton *fadeInButton = nullptr;
        TextButton *fadeOutButton = nullptr;
        TextButton *cancelButton = nullptr;
        TextButton *applyButton = nullptr;

        double startPercent = 100.0;
        double endPercent = 100.0;
        bool lockEnabled = false;
        bool syncingLockedValues = false;

        static constexpr double kMinPercent = 0.0;
        static constexpr double kMaxPercent = 1000.0;

        void setStartPercent(double value, bool refreshInput = true);
        void setEndPercent(double value, bool refreshInput = true);
        bool updatePercentControl(double &currentPercent, Slider *slider,
                                  TextInput *input, double value,
                                  bool refreshInput);
        void setLocked(bool enabled);
        void updateLockButton();
        void commitInputValues();
        void closeNow();
        void applyEffect();
        void syncSettings() const;
        void setDefaults();
        void applyFadeInPreset();
        void applyFadeOutPreset();
        void bindTextInputs();
        void bindButtons();
        void bindDropdown();
        void layoutComponents() const;
        void renderOnce() const;
        void syncInputs();

        static std::string formatPercent(double value);
        static bool tryParsePercent(const std::string &text, double &valueOut);
    };
} // namespace cupuacu::gui
