#include "ExportAudioDialogWindow.hpp"

#include "../State.hpp"
#include "../actions/ShowSaveFileDialog.hpp"

#include "Colors.hpp"
#include "UiScale.hpp"
#include "text.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <utility>
#include <vector>

namespace
{
    constexpr int kWindowWidth = 520;
    constexpr int kWindowHeight = 300;

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
    ExportAudioDialogWindow::ExportAudioDialogWindow(State *stateToUse)
        : state(stateToUse), availableFormats(file::probeAvailableExportFormats())
    {
        if (!state)
        {
            return;
        }

        window = std::make_unique<Window>(
            state, "Export Audio", kWindowWidth, kWindowHeight,
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

        auto root = std::make_unique<Component>(state, "ExportAudioDialogRoot");
        root->setVisible(true);

        background = root->emplaceChild<OpaqueRect>(state, Colors::background);
        containerLabel = root->emplaceChild<Label>(state, "Container");
        codecLabel = root->emplaceChild<Label>(state, "Codec");
        encodingLabel = root->emplaceChild<Label>(state, "Encoding");
        detailsLabel = root->emplaceChild<Label>(state);
        containerDropdown = root->emplaceChild<DropdownMenu>(state);
        codecDropdown = root->emplaceChild<DropdownMenu>(state);
        encodingDropdown = root->emplaceChild<DropdownMenu>(state);
        cancelButton = root->emplaceChild<TextButton>(state, "Cancel");
        nextButton = root->emplaceChild<TextButton>(state, "Next...");

        containerLabel->setFontSize(state->menuFontSize);
        codecLabel->setFontSize(state->menuFontSize);
        encodingLabel->setFontSize(state->menuFontSize);
        detailsLabel->setFontSize(state->menuFontSize - 8);
        containerDropdown->setFontSize(state->menuFontSize);
        codecDropdown->setFontSize(state->menuFontSize);
        encodingDropdown->setFontSize(state->menuFontSize);
        cancelButton->setTriggerOnMouseUp(true);
        nextButton->setTriggerOnMouseUp(true);

        containerDropdown->setOnSelectionChanged(
            [this](const int)
            {
                refreshCodecItems();
                refreshEncodingItems();
                refreshDetailsLabel();
            });
        codecDropdown->setOnSelectionChanged(
            [this](const int)
            {
                refreshEncodingItems();
                refreshDetailsLabel();
            });
        encodingDropdown->setOnSelectionChanged(
            [this](const int) { refreshDetailsLabel(); });

        const auto beginExport = [this]()
        {
            if (const auto settings = selectedSettings(); settings.has_value())
            {
                actions::showSaveFileDialog(state, *settings);
                requestClose();
            }
        };
        cancelButton->setOnPress([this]() { requestClose(); });
        nextButton->setOnPress(beginExport);

        refreshContainerItems();
        refreshCodecItems();
        refreshEncodingItems();
        refreshDetailsLabel();

        window->setOnResize([this]() { layoutComponents(); });
        window->setDefaultAction(beginExport);
        window->setCancelAction([this]() { requestClose(); });
        window->setRootComponent(std::move(root));
        layoutComponents();
        window->renderFrame();
    }

    ExportAudioDialogWindow::~ExportAudioDialogWindow()
    {
        detachFromState();
    }

    bool ExportAudioDialogWindow::isOpen() const
    {
        return window && window->isOpen();
    }

    void ExportAudioDialogWindow::raise() const
    {
        if (window && window->getSdlWindow())
        {
            SDL_RaiseWindow(window->getSdlWindow());
        }
    }

    void ExportAudioDialogWindow::requestClose()
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

    void ExportAudioDialogWindow::detachFromState()
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

    void ExportAudioDialogWindow::layoutComponents() const
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
        const auto [containerTextWidth, _containerTextHeight] =
            measureText("Container", state->menuFontSize);
        const auto [codecTextWidth, _codecTextHeight] =
            measureText("Codec", state->menuFontSize);
        const auto [encodingTextWidth, _encodingTextHeight] =
            measureText("Encoding", state->menuFontSize);
        const int labelWidth =
            std::max({containerTextWidth, codecTextWidth, encodingTextWidth}) +
            scaleUi(state, 20.0f);
        const int rowHeight = scaleUi(state, 42.0f);
        const int detailHeight = scaleUi(state, 48.0f);
        const int buttonWidth = scaleUi(state, 110.0f);
        const int buttonHeight = scaleUi(state, 40.0f);
        const int fieldX = padding + labelWidth;
        const int fieldWidth = width - fieldX - padding;

        window->getRootComponent()->setBounds(0, 0, width, height);
        background->setBounds(0, 0, width, height);

        containerLabel->setBounds(padding, padding, labelWidth, rowHeight);
        containerDropdown->setBounds(fieldX, padding, fieldWidth, rowHeight);
        codecLabel->setBounds(padding, padding + rowHeight + padding, labelWidth,
                              rowHeight);
        codecDropdown->setBounds(fieldX, padding + rowHeight + padding, fieldWidth,
                                 rowHeight);
        encodingLabel->setBounds(
            padding, padding + (rowHeight + padding) * 2, labelWidth, rowHeight);
        encodingDropdown->setBounds(fieldX, padding + (rowHeight + padding) * 2,
                                    fieldWidth, rowHeight);
        detailsLabel->setBounds(
            padding, padding + (rowHeight + padding) * 3, width - padding * 2,
            detailHeight);

        const int buttonY = height - padding - buttonHeight;
        cancelButton->setBounds(width - padding - buttonWidth * 2 - padding,
                                buttonY, buttonWidth, buttonHeight);
        nextButton->setBounds(width - padding - buttonWidth, buttonY, buttonWidth,
                              buttonHeight);
    }

    void ExportAudioDialogWindow::refreshContainerItems()
    {
        std::vector<std::string> items;
        for (const auto &format : availableFormats)
        {
            if (std::find(items.begin(), items.end(), format.containerLabel) ==
                items.end())
            {
                items.push_back(format.containerLabel);
            }
        }

        containerDropdown->setItems(items);
        containerDropdown->setSelectedIndex(items.empty() ? -1 : 0);
    }

    std::vector<const file::AudioExportFormatOption *>
    ExportAudioDialogWindow::currentContainerFormats() const
    {
        std::vector<const file::AudioExportFormatOption *> result;
        const int selectedIndex =
            containerDropdown ? containerDropdown->getSelectedIndex() : -1;
        if (selectedIndex < 0)
        {
            return result;
        }

        std::vector<std::string> containers;
        for (const auto &format : availableFormats)
        {
            if (std::find(containers.begin(), containers.end(),
                          format.containerLabel) == containers.end())
            {
                containers.push_back(format.containerLabel);
            }
        }
        if (selectedIndex >= static_cast<int>(containers.size()))
        {
            return result;
        }

        const auto &selectedContainer = containers[static_cast<std::size_t>(selectedIndex)];
        for (const auto &format : availableFormats)
        {
            if (format.containerLabel == selectedContainer)
            {
                result.push_back(&format);
            }
        }
        return result;
    }

    void ExportAudioDialogWindow::refreshCodecItems()
    {
        std::vector<std::string> items;
        for (const auto *format : currentContainerFormats())
        {
            if (std::find(items.begin(), items.end(), format->codecLabel) ==
                items.end())
            {
                items.push_back(format->codecLabel);
            }
        }

        codecDropdown->setItems(items);
        codecDropdown->setSelectedIndex(items.empty() ? -1 : 0);
    }

    const file::AudioExportFormatOption *
    ExportAudioDialogWindow::selectedFormatOption() const
    {
        const auto formats = currentContainerFormats();
        const int selectedIndex = codecDropdown ? codecDropdown->getSelectedIndex() : -1;
        if (selectedIndex < 0 || selectedIndex >= static_cast<int>(formats.size()))
        {
            return nullptr;
        }
        return formats[static_cast<std::size_t>(selectedIndex)];
    }

    void ExportAudioDialogWindow::refreshEncodingItems()
    {
        std::vector<std::string> items;
        if (const auto *format = selectedFormatOption())
        {
            for (const auto &encoding : format->encodings)
            {
                items.push_back(encoding.label);
            }
        }

        encodingDropdown->setItems(items);
        encodingDropdown->setSelectedIndex(items.empty() ? -1 : 0);
        nextButton->setEnabled(!items.empty());
    }

    std::optional<file::AudioExportSettings>
    ExportAudioDialogWindow::selectedSettings() const
    {
        const auto *format = selectedFormatOption();
        const int encodingIndex =
            encodingDropdown ? encodingDropdown->getSelectedIndex() : -1;
        if (!format || encodingIndex < 0 ||
            encodingIndex >= static_cast<int>(format->encodings.size()))
        {
            return std::nullopt;
        }

        const auto &encoding =
            format->encodings[static_cast<std::size_t>(encodingIndex)];
        return file::AudioExportSettings{
            .container = format->container,
            .codec = format->codec,
            .majorFormat = format->majorFormat,
            .subtype = encoding.subtype,
            .containerLabel = format->containerLabel,
            .codecLabel = format->codecLabel,
            .encodingLabel = encoding.label,
            .extension = encoding.extension,
        };
    }

    void ExportAudioDialogWindow::refreshDetailsLabel()
    {
        if (const auto settings = selectedSettings(); settings.has_value())
        {
            detailsLabel->setText("Export to ." + settings->extension + " using " +
                                  settings->containerLabel + " / " +
                                  settings->codecLabel + " / " +
                                  settings->encodingLabel);
            return;
        }

        detailsLabel->setText("No supported export formats are available.");
    }
} // namespace cupuacu::gui
