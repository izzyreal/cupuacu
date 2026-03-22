#pragma once

#include "../file/AudioExport.hpp"

#include "Component.hpp"
#include "DropdownMenu.hpp"
#include "Label.hpp"
#include "OpaqueRect.hpp"
#include "TextButton.hpp"
#include "Window.hpp"

#include <memory>
#include <optional>
#include <vector>

namespace cupuacu::gui
{
    class ExportAudioDialogWindow
    {
    public:
        explicit ExportAudioDialogWindow(State *stateToUse);
        ~ExportAudioDialogWindow();

        bool isOpen() const;
        void raise() const;
        Window *getWindow() const
        {
            return window.get();
        }

    private:
        State *state = nullptr;
        std::unique_ptr<Window> window;
        std::vector<file::AudioExportFormatOption> availableFormats;
        OpaqueRect *background = nullptr;
        Label *containerLabel = nullptr;
        Label *codecLabel = nullptr;
        Label *encodingLabel = nullptr;
        Label *bitrateModeLabel = nullptr;
        Label *bitrateLabel = nullptr;
        Label *qualityLabel = nullptr;
        Label *detailsLabel = nullptr;
        DropdownMenu *containerDropdown = nullptr;
        DropdownMenu *codecDropdown = nullptr;
        DropdownMenu *encodingDropdown = nullptr;
        DropdownMenu *bitrateModeDropdown = nullptr;
        DropdownMenu *bitrateDropdown = nullptr;
        DropdownMenu *qualityDropdown = nullptr;
        TextButton *cancelButton = nullptr;
        TextButton *nextButton = nullptr;

        void requestClose();
        void detachFromState();
        void layoutComponents() const;
        void refreshContainerItems();
        void refreshCodecItems();
        void refreshEncodingItems();
        void refreshBitrateModeItems();
        void refreshBitrateItems();
        void refreshQualityItems();
        void refreshAdvancedControlVisibility();
        void refreshDetailsLabel();
        std::vector<const file::AudioExportFormatOption *>
        currentContainerFormats() const;
        const file::AudioExportFormatOption *selectedFormatOption() const;
        std::optional<file::AudioExportSettings> selectedSettings() const;
    };
} // namespace cupuacu::gui
