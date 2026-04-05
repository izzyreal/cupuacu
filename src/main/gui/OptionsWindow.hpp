#pragma once

#include <memory>
#include <optional>

#include "../State.hpp"
#include "DevicePropertiesWindow.hpp"
#include "DisplaySettingsWindow.hpp"
#include "OptionsSection.hpp"
#include "OpaqueRect.hpp"
#include "TextButton.hpp"
#include "Window.hpp"

namespace cupuacu::gui
{
    class OptionsWindow
    {
    public:
        explicit OptionsWindow(State *stateToUse,
                               OptionsSection initialSection =
                                   OptionsSection::Audio);
        ~OptionsWindow();

        bool isOpen() const
        {
            return window && window->isOpen();
        }

        void raise() const;

        Window *getWindow() const
        {
            return window.get();
        }

        OptionsSection getSelectedSection() const
        {
            return selectedSection;
        }

        void selectSection(OptionsSection section);

    private:
        State *state = nullptr;
        std::unique_ptr<Window> window;
        OpaqueRect *background = nullptr;
        OpaqueRect *sidebarBackground = nullptr;
        TextButton *audioButton = nullptr;
        TextButton *displayButton = nullptr;
        DevicePropertiesPane *audioPane = nullptr;
        DisplaySettingsPane *displayPane = nullptr;
        OptionsSection selectedSection = OptionsSection::Audio;

        void layoutComponents() const;
        void syncSidebarButtons() const;
        void renderOnce() const;
    };

    void showOptionsWindow(State *state,
                           std::optional<OptionsSection> section =
                               std::nullopt);
} // namespace cupuacu::gui
