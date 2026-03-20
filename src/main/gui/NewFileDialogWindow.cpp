#include "NewFileDialogWindow.hpp"

#include "../State.hpp"
#include "../actions/DocumentLifecycle.hpp"

#include "Colors.hpp"
#include "UiScale.hpp"

#include <SDL3/SDL.h>

#include <algorithm>

namespace
{
    constexpr int kWindowWidth = 420;
    constexpr int kWindowHeight = 220;

    constexpr Uint32 getHighDensityWindowFlag()
    {
#if defined(__linux__)
        return 0;
#else
        return SDL_WINDOW_HIGH_PIXEL_DENSITY;
#endif
    }
} // namespace

namespace cupuacu::gui
{
    NewFileDialogWindow::NewFileDialogWindow(State *stateToUse) : state(stateToUse)
    {
        if (!state)
        {
            return;
        }

        window = std::make_unique<Window>(
            state, "New File", kWindowWidth, kWindowHeight,
            getHighDensityWindowFlag());
        if (!window || !window->isOpen())
        {
            return;
        }

        state->windows.push_back(window.get());
        state->modalWindow = window.get();

        if (auto *mainWindow = state->mainDocumentSessionWindow
                                   ? state->mainDocumentSessionWindow->getWindow()
                                   : nullptr;
            mainWindow && mainWindow->getSdlWindow())
        {
            SDL_SetWindowParent(window->getSdlWindow(), mainWindow->getSdlWindow());
        }

        auto root = std::make_unique<Component>(state, "NewFileDialogRoot");
        root->setVisible(true);

        background = root->emplaceChild<OpaqueRect>(state, Colors::background);
        sampleRateLabel = root->emplaceChild<Label>(state, "Sample rate");
        bitDepthLabel = root->emplaceChild<Label>(state, "Bit depth");
        sampleRateDropdown = root->emplaceChild<DropdownMenu>(state);
        bitDepthDropdown = root->emplaceChild<DropdownMenu>(state);
        cancelButton = root->emplaceChild<TextButton>(state, "Cancel");
        okButton = root->emplaceChild<TextButton>(state, "OK");

        sampleRateLabel->setFontSize(state->menuFontSize);
        bitDepthLabel->setFontSize(state->menuFontSize);
        sampleRateDropdown->setFontSize(state->menuFontSize);
        bitDepthDropdown->setFontSize(state->menuFontSize);
        sampleRateDropdown->setItems({"11025", "22050", "44100", "48000", "96000"});
        bitDepthDropdown->setItems({"8 bit", "16 bit"});
        sampleRateDropdown->setSelectedIndex(2);
        bitDepthDropdown->setSelectedIndex(1);
        cancelButton->setTriggerOnMouseUp(true);
        okButton->setTriggerOnMouseUp(true);
        cancelButton->setOnPress([this]() { requestClose(); });
        okButton->setOnPress(
            [this]()
            {
                cupuacu::actions::createNewDocument(
                    state, selectedSampleRate(), selectedFormat());
                requestClose();
            });

        window->setOnResize([this]() { layoutComponents(); });
        window->setOnClose([this]() { detachFromState(); });
        window->setRootComponent(std::move(root));
        layoutComponents();
        window->renderFrame();
    }

    NewFileDialogWindow::~NewFileDialogWindow()
    {
        detachFromState();
    }

    bool NewFileDialogWindow::isOpen() const
    {
        return window && window->isOpen();
    }

    void NewFileDialogWindow::raise() const
    {
        if (window && window->getSdlWindow())
        {
            SDL_RaiseWindow(window->getSdlWindow());
        }
    }

    void NewFileDialogWindow::requestClose()
    {
        if (!window || !window->isOpen())
        {
            detachFromState();
            return;
        }

        SDL_Event event{};
        event.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED;
        event.window.windowID = window->getId();
        SDL_PushEvent(&event);
    }

    void NewFileDialogWindow::detachFromState()
    {
        if (!state)
        {
            return;
        }

        if (window)
        {
            const auto it =
                std::find(state->windows.begin(), state->windows.end(), window.get());
            if (it != state->windows.end())
            {
                state->windows.erase(it);
            }
            if (state->modalWindow == window.get())
            {
                state->modalWindow = nullptr;
            }
        }
    }

    void NewFileDialogWindow::layoutComponents() const
    {
        if (!window || !window->getRootComponent())
        {
            return;
        }

        float canvasW = 0.0f;
        float canvasH = 0.0f;
        SDL_GetTextureSize(window->getCanvas(), &canvasW, &canvasH);
        const int width = static_cast<int>(canvasW);
        const int height = static_cast<int>(canvasH);
        const int padding = scaleUi(state, 20.0f);
        const int labelWidth = scaleUi(state, 140.0f);
        const int rowHeight = scaleUi(state, 42.0f);
        const int buttonWidth = scaleUi(state, 100.0f);
        const int buttonHeight = scaleUi(state, 40.0f);
        const int fieldX = padding + labelWidth;
        const int fieldWidth = width - fieldX - padding;

        window->getRootComponent()->setBounds(0, 0, width, height);
        background->setBounds(0, 0, width, height);

        sampleRateLabel->setBounds(padding, padding, labelWidth, rowHeight);
        sampleRateDropdown->setBounds(fieldX, padding, fieldWidth, rowHeight);
        bitDepthLabel->setBounds(padding, padding + rowHeight + padding,
                                 labelWidth, rowHeight);
        bitDepthDropdown->setBounds(fieldX, padding + rowHeight + padding,
                                    fieldWidth, rowHeight);

        const int buttonY = height - padding - buttonHeight;
        cancelButton->setBounds(width - padding - buttonWidth * 2 - padding,
                                buttonY, buttonWidth, buttonHeight);
        okButton->setBounds(width - padding - buttonWidth, buttonY, buttonWidth,
                            buttonHeight);
    }

    int NewFileDialogWindow::selectedSampleRate() const
    {
        switch (sampleRateDropdown ? sampleRateDropdown->getSelectedIndex() : -1)
        {
            case 0:
                return 11025;
            case 1:
                return 22050;
            case 2:
                return 44100;
            case 3:
                return 48000;
            case 4:
                return 96000;
            default:
                return 44100;
        }
    }

    cupuacu::SampleFormat NewFileDialogWindow::selectedFormat() const
    {
        return (bitDepthDropdown && bitDepthDropdown->getSelectedIndex() == 0)
                   ? cupuacu::SampleFormat::PCM_S8
                   : cupuacu::SampleFormat::PCM_S16;
    }
} // namespace cupuacu::gui
