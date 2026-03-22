#include "GenerateSilenceDialogWindow.hpp"

#include "../State.hpp"
#include "../actions/audio/EditCommands.hpp"

#include "Colors.hpp"
#include "UiScale.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <optional>

namespace
{
    constexpr int kWindowWidth = 420;
    constexpr int kWindowHeight = 240;

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
    GenerateSilenceDialogWindow::GenerateSilenceDialogWindow(State *stateToUse)
        : state(stateToUse)
    {
        if (!state)
        {
            return;
        }

        window = std::make_unique<Window>(
            state, "Generate Silence", kWindowWidth, kWindowHeight,
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

        auto root = std::make_unique<Component>(state, "GenerateSilenceDialogRoot");
        root->setVisible(true);

        background = root->emplaceChild<OpaqueRect>(state, Colors::background);
        durationLabel = root->emplaceChild<Label>(state, "Duration");
        unitLabel = root->emplaceChild<Label>(state, "Units");
        durationInput = root->emplaceChild<TextInput>(state);
        unitDropdown = root->emplaceChild<DropdownMenu>(state);
        cancelButton = root->emplaceChild<TextButton>(state, "Cancel");
        okButton = root->emplaceChild<TextButton>(state, "OK");

        const int labelFontSize = state->menuFontSize;
        const int controlFontSize = std::max(1, labelFontSize - 6);
        durationLabel->setFontSize(labelFontSize);
        unitLabel->setFontSize(labelFontSize);
        durationInput->setFontSize(controlFontSize);
        durationInput->setAllowedCharacters("0123456789.");
        durationInput->setText("1");
        unitDropdown->setFontSize(labelFontSize);
        unitDropdown->setItems({"samples", "seconds", "milliseconds"});
        unitDropdown->setSelectedIndex(1);
        cancelButton->setTriggerOnMouseUp(true);
        okButton->setTriggerOnMouseUp(true);
        const auto applySilence = [this]()
        {
            if (const auto frames = durationInFrames(); frames.has_value())
            {
                cupuacu::actions::audio::performInsertSilence(state, *frames);
                requestClose();
            }
        };
        cancelButton->setOnPress([this]() { requestClose(); });
        okButton->setOnPress(applySilence);
        durationInput->setOnEditingFinished(
            [applySilence](const std::string &)
            {
                applySilence();
            });

        window->setOnResize([this]() { layoutComponents(); });
        window->setDefaultAction(applySilence);
        window->setCancelAction([this]() { requestClose(); });
        window->setRootComponent(std::move(root));
        layoutComponents();
        window->renderFrame();
    }

    GenerateSilenceDialogWindow::~GenerateSilenceDialogWindow()
    {
        detachFromState();
    }

    bool GenerateSilenceDialogWindow::isOpen() const
    {
        return window && window->isOpen();
    }

    void GenerateSilenceDialogWindow::raise() const
    {
        if (window && window->getSdlWindow())
        {
            SDL_RaiseWindow(window->getSdlWindow());
        }
    }

    void GenerateSilenceDialogWindow::requestClose()
    {
        if (!window || !window->isOpen())
        {
            detachFromState();
            return;
        }

        if (state && state->modalWindow == window.get())
        {
            state->modalWindow = nullptr;
        }

        SDL_Event event{};
        event.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED;
        event.window.windowID = window->getId();
        SDL_PushEvent(&event);
    }

    void GenerateSilenceDialogWindow::detachFromState()
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

    void GenerateSilenceDialogWindow::layoutComponents() const
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
        const int labelWidth = scaleUi(state, 120.0f);
        const int rowHeight = scaleUi(state, 42.0f);
        const int buttonWidth = scaleUi(state, 100.0f);
        const int buttonHeight = scaleUi(state, 40.0f);
        const int fieldX = padding + labelWidth;
        const int fieldWidth = width - fieldX - padding;

        window->getRootComponent()->setBounds(0, 0, width, height);
        background->setBounds(0, 0, width, height);

        durationLabel->setBounds(padding, padding, labelWidth, rowHeight);
        durationInput->setBounds(fieldX, padding, fieldWidth, rowHeight);
        unitLabel->setBounds(padding, padding + rowHeight + padding, labelWidth,
                             rowHeight);
        unitDropdown->setBounds(fieldX, padding + rowHeight + padding,
                                fieldWidth, rowHeight);

        const int buttonY = height - padding - buttonHeight;
        cancelButton->setBounds(width - padding - buttonWidth * 2 - padding,
                                buttonY, buttonWidth, buttonHeight);
        okButton->setBounds(width - padding - buttonWidth, buttonY, buttonWidth,
                            buttonHeight);
    }

    std::optional<int64_t> GenerateSilenceDialogWindow::durationInFrames() const
    {
        if (!state)
        {
            return std::nullopt;
        }

        const auto &doc = state->getActiveDocumentSession().document;
        if (doc.getChannelCount() <= 0 || doc.getSampleRate() <= 0)
        {
            return std::nullopt;
        }

        try
        {
            std::size_t consumed = 0;
            const double parsed =
                std::stod(durationInput ? durationInput->getText() : "", &consumed);
            if (!std::isfinite(parsed) || consumed == 0 || parsed <= 0.0)
            {
                return std::nullopt;
            }

            double frames = parsed;
            switch (unitDropdown ? unitDropdown->getSelectedIndex() : 0)
            {
                case 1:
                    frames *= static_cast<double>(doc.getSampleRate());
                    break;
                case 2:
                    frames *= static_cast<double>(doc.getSampleRate()) / 1000.0;
                    break;
                case 0:
                default:
                    break;
            }

            const int64_t rounded = static_cast<int64_t>(std::llround(frames));
            if (rounded <= 0)
            {
                return std::nullopt;
            }

            return rounded;
        }
        catch (...)
        {
            return std::nullopt;
        }
    }
} // namespace cupuacu::gui
