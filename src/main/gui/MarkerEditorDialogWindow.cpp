#include "MarkerEditorDialogWindow.hpp"

#include "../actions/markers/EditCommands.hpp"

#include "Colors.hpp"
#include "SecondaryWindowLifecycle.hpp"
#include "SnapPlanning.hpp"
#include "UiScale.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>

namespace
{
    constexpr int kWindowWidth = 760;
    constexpr int kWindowHeight = 420;
    constexpr SDL_Color kSidebarColor{34, 34, 34, 255};
    constexpr SDL_Color kSidebarActiveColor{74, 110, 170, 255};
}

namespace cupuacu::gui
{
    MarkerEditorDialogWindow::MarkerEditorDialogWindow(
        State *stateToUse, const std::optional<uint64_t> initialMarkerIdToUse)
        : state(stateToUse), selectedMarkerId(initialMarkerIdToUse)
    {
        if (!state)
        {
            return;
        }

        window = std::make_unique<Window>(
            state, "Edit markers", kWindowWidth, kWindowHeight,
            SDL_WINDOW_HIGH_PIXEL_DENSITY);
        if (!window || !window->isOpen())
        {
            return;
        }

        attachSecondaryWindow(state, window.get(), true);

        auto root = std::make_unique<Component>(state, "MarkerEditorDialogRoot");
        root->setVisible(true);

        background = root->emplaceChild<OpaqueRect>(state, Colors::background);
        sidebarBackground = root->emplaceChild<OpaqueRect>(state, kSidebarColor);
        sidebarList = root->emplaceChild<Component>(state, "MarkerEditorSidebarList");
        emptySidebarLabel = root->emplaceChild<Label>(state, "No markers");
        addButton = root->emplaceChild<TextButton>(state, "Add marker");
        nameLabel = root->emplaceChild<Label>(state, "Name");
        positionLabel = root->emplaceChild<Label>(state, "Position");
        nameInput = root->emplaceChild<TextInput>(state);
        positionInput = root->emplaceChild<TextInput>(state);
        emptyStateLabel = root->emplaceChild<Label>(
            state, "No marker selected.\nUse Add marker to create one.");
        closeButton = root->emplaceChild<TextButton>(state, "Close");
        deleteButton = root->emplaceChild<TextButton>(state, "Delete");
        applyButton = root->emplaceChild<TextButton>(state, "Apply");

        const int labelFontSize = state->menuFontSize;
        const int controlFontSize = std::max(1, labelFontSize - 6);
        emptySidebarLabel->setFontSize(labelFontSize);
        nameLabel->setFontSize(labelFontSize);
        positionLabel->setFontSize(labelFontSize);
        emptyStateLabel->setFontSize(labelFontSize);
        nameInput->setFontSize(controlFontSize);
        positionInput->setFontSize(controlFontSize);
        positionInput->setAllowedCharacters("0123456789");
        addButton->setTriggerOnMouseUp(true);
        closeButton->setTriggerOnMouseUp(true);
        deleteButton->setTriggerOnMouseUp(true);
        applyButton->setTriggerOnMouseUp(true);

        addButton->setOnPress([this]() { addMarker(); });
        closeButton->setOnPress([this]() { requestClose(); });
        deleteButton->setOnPress([this]() { deleteMarker(); });
        applyButton->setOnPress([this]() { applyChanges(); });
        nameInput->setOnEditingFinished(
            [this](const std::string &)
            {
                applyChanges();
                syncFromSelectedMarker();
            });
        positionInput->setOnEditingFinished(
            [this](const std::string &)
            {
                applyChanges();
                syncFromSelectedMarker();
            });

        window->setOnResize(
            [this]()
            {
                rebuildMarkerButtons();
                layoutComponents();
            });
        window->setDefaultAction([this]() { applyChanges(); });
        window->setCancelAction([this]() { requestClose(); });
        window->setOnClose([this]() { detachFromState(); });
        window->setRootComponent(std::move(root));
        rebuildMarkerButtons();
        syncFromSelectedMarker();
        layoutComponents();
        window->renderFrame();
    }

    MarkerEditorDialogWindow::~MarkerEditorDialogWindow()
    {
        detachFromState();
    }

    bool MarkerEditorDialogWindow::isOpen() const
    {
        return window && window->isOpen();
    }

    void MarkerEditorDialogWindow::raise() const
    {
        raiseSecondaryWindow(window.get());
    }

    void MarkerEditorDialogWindow::selectMarker(
        const std::optional<uint64_t> markerIdToSelect)
    {
        selectedMarkerId = markerIdToSelect;
        if (state)
        {
            state->getActiveViewState().selectedMarkerId = selectedMarkerId;
        }
        syncFromSelectedMarker();
        syncSidebarButtons();
        if (window)
        {
            window->renderFrame();
        }
    }

    void MarkerEditorDialogWindow::requestClose()
    {
        if (!window || !window->isOpen())
        {
            detachFromState();
            return;
        }
        requestSecondaryWindowClose(state, window.get());
    }

    void MarkerEditorDialogWindow::detachFromState()
    {
        if (!state)
        {
            return;
        }

        detachSecondaryWindow(state, window.get());
    }

    void MarkerEditorDialogWindow::layoutComponents() const
    {
        if (!window || !window->getRootComponent())
        {
            return;
        }

        float canvasWidth = 0.0f;
        float canvasHeight = 0.0f;
        SDL_GetTextureSize(window->getCanvas(), &canvasWidth, &canvasHeight);
        const int width = static_cast<int>(canvasWidth);
        const int height = static_cast<int>(canvasHeight);
        const int padding = scaleUi(state, 16.0f);
        const int gap = std::max(6, scaleUi(state, 8.0f));
        const int sidebarWidth = std::max(scaleUi(state, 220.0f),
                                          scaleUi(state, 180.0f));
        const int labelWidth = scaleUi(state, 120.0f);
        const int rowHeight = scaleUi(state, 42.0f);
        const int buttonWidth = scaleUi(state, 100.0f);
        const int buttonHeight = scaleUi(state, 40.0f);
        const int listTop = padding;
        const int addButtonHeight = scaleUi(state, 42.0f);
        const int addButtonY = height - padding - addButtonHeight;
        const int listHeight =
            std::max(0, addButtonY - listTop - gap);
        const int contentX = sidebarWidth + padding;
        const int contentWidth = std::max(0, width - contentX - padding);
        const int fieldX = contentX + labelWidth;
        const int fieldWidth = std::max(0, width - fieldX - padding);

        window->getRootComponent()->setBounds(0, 0, width, height);
        background->setBounds(0, 0, width, height);
        sidebarBackground->setBounds(0, 0, sidebarWidth, height);
        sidebarList->setBounds(padding, listTop, sidebarWidth - padding * 2,
                               listHeight);
        emptySidebarLabel->setBounds(padding, listTop, sidebarWidth - padding * 2,
                                     rowHeight);
        addButton->setBounds(padding, addButtonY, sidebarWidth - padding * 2,
                             addButtonHeight);

        nameLabel->setBounds(contentX, padding, labelWidth, rowHeight);
        nameInput->setBounds(fieldX, padding, fieldWidth, rowHeight);
        positionLabel->setBounds(contentX, padding + rowHeight + padding,
                                 labelWidth, rowHeight);
        positionInput->setBounds(fieldX, padding + rowHeight + padding, fieldWidth,
                                 rowHeight);
        emptyStateLabel->setBounds(
            contentX, padding, contentWidth,
            std::max(rowHeight * 2, height - padding * 3 - buttonHeight));

        const int buttonY = height - padding - buttonHeight;
        closeButton->setBounds(width - padding - buttonWidth * 3 - padding * 2,
                               buttonY, buttonWidth, buttonHeight);
        deleteButton->setBounds(width - padding - buttonWidth * 2 - padding,
                                buttonY, buttonWidth, buttonHeight);
        applyButton->setBounds(width - padding - buttonWidth, buttonY, buttonWidth,
                               buttonHeight);

        const bool hasSelection = selectedMarkerId.has_value();
        nameLabel->setVisible(hasSelection);
        positionLabel->setVisible(hasSelection);
        nameInput->setVisible(hasSelection);
        positionInput->setVisible(hasSelection);
        deleteButton->setVisible(hasSelection);
        applyButton->setVisible(hasSelection);
        emptyStateLabel->setVisible(!hasSelection);
        emptySidebarLabel->setVisible(markerButtons.empty());
    }

    std::optional<int64_t> MarkerEditorDialogWindow::parsedFrame() const
    {
        try
        {
            std::size_t consumed = 0;
            const int64_t parsed = std::stoll(
                positionInput ? positionInput->getText() : std::string{}, &consumed);
            if (consumed == 0)
            {
                return std::nullopt;
            }

            return std::max<int64_t>(0, parsed);
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    void MarkerEditorDialogWindow::rebuildMarkerButtons()
    {
        if (!sidebarList || !state)
        {
            return;
        }

        sidebarList->removeChildrenOfType<TextButton>();
        markerButtons.clear();

        const auto &markers = state->getActiveDocumentSession().document.getMarkers();
        lastMarkerDataVersion =
            state->getActiveDocumentSession().document.getMarkerDataVersion();
        for (const auto &marker : markers)
        {
            auto *button =
                sidebarList->emplaceChild<TextButton>(state, "Marker");
            button->setTriggerOnMouseUp(true);
            button->setFontSize(state->menuFontSize);
            button->setOnPress(
                [this, markerId = marker.id]()
                {
                    selectMarker(markerId);
                });
            markerButtons.push_back(button);
        }

        syncSidebarButtons();
    }

    void MarkerEditorDialogWindow::syncFromSelectedMarker()
    {
        if (!state)
        {
            return;
        }

        const auto markerDataVersion =
            state->getActiveDocumentSession().document.getMarkerDataVersion();
        if (markerDataVersion != lastMarkerDataVersion)
        {
            rebuildMarkerButtons();
        }

        if (selectedMarkerId.has_value())
        {
            const auto marker = actions::markers::findMarkerById(
                state->getActiveDocumentSession().document, *selectedMarkerId);
            if (!marker.has_value())
            {
                selectedMarkerId.reset();
            }
        }

        if (selectedMarkerId.has_value())
        {
            const auto marker = actions::markers::findMarkerById(
                state->getActiveDocumentSession().document, *selectedMarkerId);
            if (marker.has_value())
            {
                nameInput->setText(marker->label);
                positionInput->setText(std::to_string(marker->frame));
            }
        }
        else
        {
            nameInput->setText({});
            positionInput->setText({});
        }

        if (state)
        {
            state->getActiveViewState().selectedMarkerId = selectedMarkerId;
        }

        syncSidebarButtons();
        layoutComponents();
    }

    void MarkerEditorDialogWindow::syncSidebarButtons() const
    {
        if (!state || !sidebarList)
        {
            return;
        }

        const auto &markers = state->getActiveDocumentSession().document.getMarkers();
        for (std::size_t index = 0; index < markerButtons.size() && index < markers.size();
             ++index)
        {
            auto *button = markerButtons[index];
            const auto &marker = markers[index];
            std::string label = std::to_string(marker.frame);
            if (!marker.label.empty())
            {
                label += "  " + marker.label;
            }
            else
            {
                label += "  Marker " + std::to_string(index + 1);
            }
            button->setText(label);
            button->setForcedFillColor(
                selectedMarkerId.has_value() && *selectedMarkerId == marker.id
                    ? std::optional<SDL_Color>{kSidebarActiveColor}
                    : std::nullopt);
        }

        const int gap = std::max(6, scaleUi(state, 8.0f));
        const int buttonHeight = scaleUi(state, 40.0f);
        const int width = sidebarList->getWidth();
        for (std::size_t index = 0; index < markerButtons.size(); ++index)
        {
            markerButtons[index]->setBounds(
                0, static_cast<int>(index) * (buttonHeight + gap), width,
                buttonHeight);
        }
    }

    void MarkerEditorDialogWindow::applyChanges()
    {
        if (!state)
        {
            return;
        }

        syncFromSelectedMarker();
        if (!selectedMarkerId.has_value())
        {
            return;
        }

        const auto oldState =
            actions::markers::currentMarkerSnapshot(state, *selectedMarkerId);
        const auto frame = parsedFrame();
        if (!oldState.has_value() || !frame.has_value())
        {
            return;
        }

        auto newState = *oldState;
        newState.marker.frame = *frame;
        newState.marker.label = nameInput ? nameInput->getText() : std::string{};
        newState.selectedMarkerId = selectedMarkerId;

        if (newState.marker == oldState->marker)
        {
            return;
        }

        state->addAndDoUndoable(std::make_shared<actions::markers::SetMarkerState>(
            state, *oldState, newState, "Edit marker"));
        syncFromSelectedMarker();
        if (window)
        {
            window->renderFrame();
        }
    }

    void MarkerEditorDialogWindow::deleteMarker()
    {
        if (!state || !selectedMarkerId.has_value())
        {
            return;
        }

        const auto &markers = state->getActiveDocumentSession().document.getMarkers();
        std::optional<uint64_t> nextSelectedMarkerId;
        for (std::size_t index = 0; index < markers.size(); ++index)
        {
            if (markers[index].id != *selectedMarkerId)
            {
                continue;
            }
            if (index + 1 < markers.size())
            {
                nextSelectedMarkerId = markers[index + 1].id;
            }
            else if (index > 0)
            {
                nextSelectedMarkerId = markers[index - 1].id;
            }
            break;
        }

        actions::markers::deleteMarker(state, *selectedMarkerId, nextSelectedMarkerId);
        selectedMarkerId = nextSelectedMarkerId;
        syncFromSelectedMarker();
        if (window)
        {
            window->renderFrame();
        }
    }

    void MarkerEditorDialogWindow::addMarker()
    {
        if (!state)
        {
            return;
        }

        const uint64_t markerId = actions::markers::insertMarkerAtCursor(state);
        if (markerId == 0)
        {
            return;
        }

        selectMarker(markerId);
        if (window)
        {
            window->renderFrame();
        }
    }
} // namespace cupuacu::gui
