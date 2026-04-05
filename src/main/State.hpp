#pragma once

#include <SDL3/SDL.h>

#include "gui/Selection.hpp"

#include "SelectedChannels.hpp"
#include "DocumentTab.hpp"
#include "effects/EffectSettings.hpp"
#include "Paths.hpp"
#include "gui/DocumentSessionWindow.hpp"
#include "gui/VuMeterScale.hpp"

#include <cstdint>
#include <functional>
#include <vector>
#include <string>
#include <memory>
#include <optional>
#include <deque>

namespace cupuacu
{
    namespace audio
    {
        class AudioDevices;
    } // namespace audio

    namespace actions
    {
        struct CustomDataSource;
        class Undoable;
    } // namespace actions

    namespace gui
    {
        class DevicePropertiesWindow;
        class DisplaySettingsWindow;
        class NewFileDialogWindow;
        class GenerateSilenceDialogWindow;
        class ExportAudioDialogWindow;
        class Component;
        class Waveform;
    } // namespace gui

    namespace effects
    {
        class AmplifyEnvelopeDialog;
        class AmplifyFadeDialog;
        class DynamicsDialog;
        class RemoveSilenceDialog;
    } // namespace effects

    void destroyAmplifyEnvelopeDialog(effects::AmplifyEnvelopeDialog *);
    void destroyAmplifyFadeDialog(effects::AmplifyFadeDialog *);
    void destroyDynamicsDialog(effects::DynamicsDialog *);
    void destroyRemoveSilenceDialog(effects::RemoveSilenceDialog *);
    void destroyDisplaySettingsWindow(gui::DisplaySettingsWindow *);
    void destroyNewFileDialogWindow(gui::NewFileDialogWindow *);
    void destroyGenerateSilenceDialogWindow(gui::GenerateSilenceDialogWindow *);
    void destroyExportAudioDialogWindow(gui::ExportAudioDialogWindow *);

    struct State
    {
        std::shared_ptr<audio::AudioDevices> audioDevices;
        std::unique_ptr<Paths> paths = std::make_unique<Paths>();
        uint8_t menuFontSize = 30;
        uint8_t pixelScale = 1;
        float uiScale = 1.0f;
        gui::VuMeterScale vuMeterScale = gui::VuMeterScale::PeakDbfs;
        bool loopPlaybackEnabled = false;
        uint64_t playbackRangeStart = 0;
        uint64_t playbackRangeEnd = 0;
        std::vector<DocumentTab> tabs{DocumentTab{}};
        int activeTabIndex = 0;
        Document clipboard;
        effects::EffectSettings effectSettings;
        std::vector<std::string> recentFiles;

        std::vector<gui::Waveform *> waveforms;
        std::vector<gui::Window *> windows;
        std::unique_ptr<gui::DocumentSessionWindow> mainDocumentSessionWindow;
        std::unique_ptr<gui::DevicePropertiesWindow> devicePropertiesWindow;
        std::unique_ptr<gui::DisplaySettingsWindow,
                        void (*)(gui::DisplaySettingsWindow *)>
            displaySettingsWindow{nullptr, destroyDisplaySettingsWindow};
        std::unique_ptr<gui::NewFileDialogWindow,
                        void (*)(gui::NewFileDialogWindow *)>
            newFileDialogWindow{nullptr, destroyNewFileDialogWindow};
        std::unique_ptr<gui::GenerateSilenceDialogWindow,
                        void (*)(gui::GenerateSilenceDialogWindow *)>
            generateSilenceDialogWindow{nullptr,
                                        destroyGenerateSilenceDialogWindow};
        std::unique_ptr<gui::ExportAudioDialogWindow,
                        void (*)(gui::ExportAudioDialogWindow *)>
            exportAudioDialogWindow{nullptr, destroyExportAudioDialogWindow};
        std::unique_ptr<effects::AmplifyFadeDialog,
                        void (*)(effects::AmplifyFadeDialog *)>
            amplifyFadeDialog{nullptr, destroyAmplifyFadeDialog};
        std::unique_ptr<effects::AmplifyEnvelopeDialog,
                        void (*)(effects::AmplifyEnvelopeDialog *)>
            amplifyEnvelopeDialog{nullptr, destroyAmplifyEnvelopeDialog};
        std::unique_ptr<effects::DynamicsDialog,
                        void (*)(effects::DynamicsDialog *)>
            dynamicsDialog{nullptr, destroyDynamicsDialog};
        std::unique_ptr<effects::RemoveSilenceDialog,
                        void (*)(effects::RemoveSilenceDialog *)>
            removeSilenceDialog{nullptr, destroyRemoveSilenceDialog};
        std::optional<file::AudioExportSettings> pendingSaveAsExportSettings;
        std::function<void(const std::string &, const std::string &)>
            errorReporter;
        std::optional<std::pair<std::string, std::string>>
            pendingStartupWarning;
        gui::Window *modalWindow = nullptr;

        ~State();

        DocumentTab *getActiveTab()
        {
            if (tabs.empty())
            {
                tabs.emplace_back();
                activeTabIndex = 0;
            }
            activeTabIndex = std::clamp(activeTabIndex, 0,
                                        static_cast<int>(tabs.size()) - 1);
            return &tabs[activeTabIndex];
        }

        const DocumentTab *getActiveTab() const
        {
            if (tabs.empty() || activeTabIndex < 0 ||
                activeTabIndex >= static_cast<int>(tabs.size()))
            {
                return nullptr;
            }
            return &tabs[activeTabIndex];
        }

        DocumentSession &getActiveDocumentSession()
        {
            return getActiveTab()->session;
        }

        const DocumentSession &getActiveDocumentSession() const
        {
            return getActiveTab()->session;
        }

        gui::EditorViewState &getActiveViewState()
        {
            return getActiveTab()->viewState;
        }

        const gui::EditorViewState &getActiveViewState() const
        {
            return getActiveTab()->viewState;
        }

        std::deque<std::shared_ptr<actions::Undoable>> &getActiveUndoables()
        {
            return getActiveTab()->undoables;
        }

        const std::deque<std::shared_ptr<actions::Undoable>> &
        getActiveUndoables() const
        {
            return getActiveTab()->undoables;
        }

        std::deque<std::shared_ptr<actions::Undoable>> &getActiveRedoables()
        {
            return getActiveTab()->redoables;
        }

        const std::deque<std::shared_ptr<actions::Undoable>> &
        getActiveRedoables() const
        {
            return getActiveTab()->redoables;
        }

        void addUndoable(std::shared_ptr<actions::Undoable>);
        void addAndDoUndoable(std::shared_ptr<actions::Undoable>);
        void undo();
        void redo();
        bool canUndo()
        {
            return !getActiveUndoables().empty();
        }
        bool canRedo()
        {
            return !getActiveRedoables().empty();
        }
        std::string getUndoDescription();
        std::string getRedoDescription();
    };
} // namespace cupuacu

static void resetSampleValueUnderMouseCursor(cupuacu::State *state)
{
    if (state->mainDocumentSessionWindow)
    {
        state->getActiveViewState().sampleValueUnderMouseCursor.reset();
    }
}

static void updateSampleValueUnderMouseCursor(cupuacu::State *state,
                                              const float sampleValue,
                                              const int64_t channel,
                                              const int64_t frame)
{
    if (state->mainDocumentSessionWindow)
    {
        state->getActiveViewState().sampleValueUnderMouseCursor.emplace(
            cupuacu::gui::HoveredSampleInfo{sampleValue, channel, frame});
    }
}

static void resetWaveformState(cupuacu::State *state)
{
    if (state->mainDocumentSessionWindow)
    {
        auto &viewState = state->getActiveViewState();
        viewState.verticalZoom = 1;
        viewState.sampleOffset = 0;
        viewState.selectedChannels = cupuacu::SelectedChannels::BOTH;
        viewState.samplesToScroll = 0;
    }
    state->getActiveDocumentSession().selection.reset();
}

int64_t getMaxSampleOffset(const cupuacu::State *);

static void updateSampleOffset(cupuacu::State *state,
                               const int64_t sampleOffset)
{
    if (!state->mainDocumentSessionWindow)
    {
        return;
    }
    auto &viewState = state->getActiveViewState();
    viewState.sampleOffset =
        std::clamp(sampleOffset, int64_t{0}, getMaxSampleOffset(state));
}

static bool updateCursorPos(cupuacu::State *state, const int64_t cursorPos)
{
    auto &session = state->getActiveDocumentSession();
    const int64_t oldCursor = session.cursor;
    session.cursor =
        std::clamp(cursorPos, int64_t{0}, session.document.getFrameCount());
    return session.cursor != oldCursor;
}
