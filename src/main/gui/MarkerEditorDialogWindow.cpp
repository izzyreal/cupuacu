#include "MarkerEditorDialogWindow.hpp"

#include "../actions/markers/EditCommands.hpp"

#include "Colors.hpp"
#include "SecondaryWindowLifecycle.hpp"
#include "UiScale.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>

namespace
{
    constexpr int kWindowWidth = 460;
    constexpr int kWindowHeight = 240;
}

namespace cupuacu::gui
{
    MarkerEditorDialogWindow::MarkerEditorDialogWindow(
        State *stateToUse, const uint64_t markerIdToUse)
        : state(stateToUse), markerId(markerIdToUse)
    {
        if (!state)
        {
            return;
        }

        window = std::make_unique<Window>(
            state, "Edit Marker", kWindowWidth, kWindowHeight,
            SDL_WINDOW_HIGH_PIXEL_DENSITY);
        if (!window || !window->isOpen())
        {
            return;
        }

        attachSecondaryWindow(state, window.get(), true);

        auto root = std::make_unique<Component>(state, "MarkerEditorDialogRoot");
        root->setVisible(true);

        background = root->emplaceChild<OpaqueRect>(state, Colors::background);
        nameLabel = root->emplaceChild<Label>(state, "Name");
        positionLabel = root->emplaceChild<Label>(state, "Position");
        nameInput = root->emplaceChild<TextInput>(state);
        positionInput = root->emplaceChild<TextInput>(state);
        cancelButton = root->emplaceChild<TextButton>(state, "Cancel");
        deleteButton = root->emplaceChild<TextButton>(state, "Delete");
        okButton = root->emplaceChild<TextButton>(state, "OK");

        const auto marker = actions::markers::findMarkerById(
            state->getActiveDocumentSession().document, markerId);
        if (marker.has_value())
        {
            nameInput->setText(marker->label);
            positionInput->setText(std::to_string(marker->frame));
        }

        const int labelFontSize = state->menuFontSize;
        const int controlFontSize = std::max(1, labelFontSize - 6);
        nameLabel->setFontSize(labelFontSize);
        positionLabel->setFontSize(labelFontSize);
        nameInput->setFontSize(controlFontSize);
        positionInput->setFontSize(controlFontSize);
        positionInput->setAllowedCharacters("0123456789");
        cancelButton->setTriggerOnMouseUp(true);
        deleteButton->setTriggerOnMouseUp(true);
        okButton->setTriggerOnMouseUp(true);

        cancelButton->setOnPress([this]() { requestClose(); });
        deleteButton->setOnPress([this]() { deleteMarker(); });
        okButton->setOnPress([this]() { applyChanges(); });
        nameInput->setOnEditingFinished(
            [this](const std::string &) { applyChanges(); });
        positionInput->setOnEditingFinished(
            [this](const std::string &) { applyChanges(); });

        window->setOnResize([this]() { layoutComponents(); });
        window->setDefaultAction([this]() { applyChanges(); });
        window->setCancelAction([this]() { requestClose(); });
        window->setOnClose([this]() { detachFromState(); });
        window->setRootComponent(std::move(root));
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
        const int padding = scaleUi(state, 20.0f);
        const int labelWidth = scaleUi(state, 120.0f);
        const int rowHeight = scaleUi(state, 42.0f);
        const int buttonWidth = scaleUi(state, 100.0f);
        const int buttonHeight = scaleUi(state, 40.0f);
        const int fieldX = padding + labelWidth;
        const int fieldWidth = width - fieldX - padding;

        window->getRootComponent()->setBounds(0, 0, width, height);
        background->setBounds(0, 0, width, height);
        nameLabel->setBounds(padding, padding, labelWidth, rowHeight);
        nameInput->setBounds(fieldX, padding, fieldWidth, rowHeight);
        positionLabel->setBounds(padding, padding + rowHeight + padding,
                                 labelWidth, rowHeight);
        positionInput->setBounds(fieldX, padding + rowHeight + padding,
                                 fieldWidth, rowHeight);

        const int buttonY = height - padding - buttonHeight;
        cancelButton->setBounds(width - padding - buttonWidth * 3 - padding * 2,
                                buttonY, buttonWidth, buttonHeight);
        deleteButton->setBounds(width - padding - buttonWidth * 2 - padding,
                                buttonY, buttonWidth, buttonHeight);
        okButton->setBounds(width - padding - buttonWidth, buttonY, buttonWidth,
                            buttonHeight);
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

    void MarkerEditorDialogWindow::applyChanges()
    {
        if (!state)
        {
            return;
        }

        const auto oldState =
            actions::markers::currentMarkerSnapshot(state, markerId);
        const auto frame = parsedFrame();
        if (!oldState.has_value() || !frame.has_value())
        {
            return;
        }

        auto newState = *oldState;
        newState.marker.frame = *frame;
        newState.marker.label = nameInput ? nameInput->getText() : std::string{};
        newState.selectedMarkerId = markerId;

        if (newState.marker == oldState->marker)
        {
            requestClose();
            return;
        }

        state->addAndDoUndoable(std::make_shared<actions::markers::SetMarkerState>(
            state, *oldState, newState, "Edit marker"));
        requestClose();
    }

    void MarkerEditorDialogWindow::deleteMarker()
    {
        if (!state)
        {
            return;
        }

        actions::markers::deleteMarker(state, markerId);
        requestClose();
    }
} // namespace cupuacu::gui
