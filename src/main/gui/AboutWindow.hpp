#pragma once

#include <memory>
#include <string>

namespace cupuacu
{
    struct State;
}

namespace cupuacu::gui
{
    class Label;
    class Component;
    class OpaqueRect;
    class SelectableTextView;
    class TextButton;
    class Window;

    enum class AboutSection
    {
        AboutAndBuild,
        CreditsAndLicenses,
    };

    class AboutWindow
    {
    public:
        explicit AboutWindow(State *stateToUse);
        ~AboutWindow();

        bool isOpen() const;
        void raise() const;
        Window *getWindow() const
        {
            return window.get();
        }
        AboutSection getSelectedSection() const
        {
            return selectedSection;
        }
        void selectSection(AboutSection section);

    private:
        State *state = nullptr;
        std::unique_ptr<Window> window;
        OpaqueRect *background = nullptr;
        Component *watermark = nullptr;
        Label *title = nullptr;
        Label *tagline = nullptr;
        Label *version = nullptr;
        TextButton *aboutButton = nullptr;
        TextButton *creditsButton = nullptr;
        SelectableTextView *textView = nullptr;
        TextButton *copyVersionButton = nullptr;
        TextButton *copyDetailsButton = nullptr;
        TextButton *projectWebsiteButton = nullptr;
        AboutSection selectedSection = AboutSection::AboutAndBuild;

        std::string rendererName() const;
        void layoutComponents() const;
        void updateContent();
        void renderOnce() const;
    };

    void showAboutWindow(State *state);
} // namespace cupuacu::gui
