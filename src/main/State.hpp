#pragma once

#include <SDL3/SDL.h>

#include "gui/Selection.hpp"

#include "SelectedChannels.hpp"
#include "DocumentSession.hpp"
#include "Paths.hpp"
#include "gui/DocumentSessionWindow.hpp"

#include <cstdint>
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
        class Component;
        class Waveform;
        class MainView;
        class VuMeter;
        class VuMeterContainer;
        class TransportButtonsContainer;
    } // namespace gui

    struct State
    {
        std::shared_ptr<audio::AudioDevices> audioDevices;
        std::unique_ptr<Paths> paths = std::make_unique<Paths>();
        std::deque<std::shared_ptr<actions::Undoable>> undoables;
        std::deque<std::shared_ptr<actions::Undoable>> redoables;
        uint8_t menuFontSize = 40;
        uint8_t pixelScale = 1;
        DocumentSession activeDocumentSession;
        Document clipboard;

        std::vector<gui::Waveform *> waveforms;
        std::vector<gui::Window *> windows;
        std::unique_ptr<gui::DocumentSessionWindow> mainDocumentSessionWindow;
        std::unique_ptr<gui::DevicePropertiesWindow> devicePropertiesWindow;
        gui::MainView *mainView;
        gui::Component *statusBar;
        gui::VuMeterContainer *vuMeterContainer;
        gui::TransportButtonsContainer *transportButtonsContainer = nullptr;
        gui::VuMeter *vuMeter;

        void addUndoable(std::shared_ptr<actions::Undoable>);
        void addAndDoUndoable(std::shared_ptr<actions::Undoable>);
        void undo();
        void redo();
        bool canUndo()
        {
            return !undoables.empty();
        }
        bool canRedo()
        {
            return !redoables.empty();
        }
        std::string getUndoDescription();
        std::string getRedoDescription();
    };
} // namespace cupuacu

static void resetSampleValueUnderMouseCursor(cupuacu::State *state)
{
    if (state->mainDocumentSessionWindow)
    {
        state->mainDocumentSessionWindow->getViewState()
            .sampleValueUnderMouseCursor.reset();
    }
}

static void updateSampleValueUnderMouseCursor(cupuacu::State *state,
                                              const float sampleValue)
{
    if (state->mainDocumentSessionWindow)
    {
        state->mainDocumentSessionWindow->getViewState()
            .sampleValueUnderMouseCursor.emplace(sampleValue);
    }
}

static void resetWaveformState(cupuacu::State *state)
{
    if (state->mainDocumentSessionWindow)
    {
        auto &viewState = state->mainDocumentSessionWindow->getViewState();
        viewState.verticalZoom = 1;
        viewState.sampleOffset = 0;
        viewState.selectedChannels = cupuacu::SelectedChannels::BOTH;
        viewState.samplesToScroll = 0;
    }
    state->activeDocumentSession.selection.reset();
}

int64_t getMaxSampleOffset(const cupuacu::State *);

static void updateSampleOffset(cupuacu::State *state,
                               const int64_t sampleOffset)
{
    if (!state->mainDocumentSessionWindow)
    {
        return;
    }
    auto &viewState = state->mainDocumentSessionWindow->getViewState();
    viewState.sampleOffset =
        std::clamp(sampleOffset, int64_t{0}, getMaxSampleOffset(state));
}

static bool updateCursorPos(cupuacu::State *state, const int64_t cursorPos)
{
    const int64_t oldCursor = state->activeDocumentSession.cursor;
    state->activeDocumentSession.cursor =
        std::clamp(cursorPos, int64_t{0},
                   state->activeDocumentSession.document.getFrameCount());
    return state->activeDocumentSession.cursor != oldCursor;
}
