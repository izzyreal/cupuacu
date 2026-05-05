#pragma once

#include "../State.hpp"
#include "../gui/MainViewAccess.hpp"
#include "../gui/Waveform.hpp"
#include "DocumentSessionPersistence.hpp"
#include "Play.hpp"
#include "ViewPolicy.hpp"
#include "Zoom.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <string>

namespace cupuacu::actions
{
    constexpr const char *kUntitledDocumentTitle = "Untitled";

    inline bool hasActiveDocument(const cupuacu::State *state)
    {
        return state &&
               state->getActiveDocumentSession().document.getChannelCount() > 0;
    }

    inline void setMainWindowTitle(cupuacu::State *state,
                                   const std::string &title)
    {
        if (!state || !state->mainDocumentSessionWindow)
        {
            return;
        }

        auto *mainWindow = state->mainDocumentSessionWindow->getWindow();
        if (!mainWindow || !mainWindow->getSdlWindow())
        {
            return;
        }

        SDL_SetWindowTitle(mainWindow->getSdlWindow(), title.c_str());
    }

    inline void prepareForDocumentTransition(cupuacu::State *state)
    {
        if (!state)
        {
            return;
        }

        if (state->audioDevices)
        {
            if (state->audioDevices->isPlaying() ||
                state->audioDevices->isRecording())
            {
                requestStop(state);
            }
            state->audioDevices->clearRecordedChunks();
        }

        state->getActiveUndoables().clear();
        state->getActiveRedoables().clear();
    }

    inline void bindMainWindowToActiveDocument(cupuacu::State *state)
    {
        if (!state || !state->mainDocumentSessionWindow)
        {
            return;
        }

        state->mainDocumentSessionWindow->bindDocumentSession(
            &state->getActiveDocumentSession(), &state->getActiveViewState());
    }

    inline bool shouldReuseLoneBlankTab(const cupuacu::State *state)
    {
        if (!state || state->tabs.size() != 1 || state->activeTabIndex != 0)
        {
            return false;
        }

        const auto *tab = state->getActiveTab();
        if (!tab)
        {
            return false;
        }

        return tab->session.currentFile.empty() &&
               tab->session.document.getChannelCount() == 0 &&
               tab->session.document.getFrameCount() == 0 &&
               tab->undoables.empty() && tab->redoables.empty();
    }

    inline bool prepareTabForOpenedDocument(cupuacu::State *state)
    {
        if (!state)
        {
            return false;
        }

        if (state->audioDevices &&
            (state->audioDevices->isPlaying() ||
             state->audioDevices->isRecording()))
        {
            return false;
        }

        if (!shouldReuseLoneBlankTab(state))
        {
            state->tabs.emplace_back();
            state->activeTabIndex = static_cast<int>(state->tabs.size()) - 1;
        }

        bindMainWindowToActiveDocument(state);
        resetSampleValueUnderMouseCursor(state);
        return true;
    }

    inline void refreshDocumentUi(cupuacu::State *state)
    {
        if (!state)
        {
            return;
        }

        gui::rebuildMainWaveforms(state);
        resetWaveformState(state);
        resetSampleValueUnderMouseCursor(state);
        resetZoom(state);
        gui::Waveform::updateAllSamplePoints(state);
        gui::Waveform::setAllWaveformsDirty(state);
        gui::requestMainViewRefresh(state);
    }

    inline void refreshBoundDocumentUi(cupuacu::State *state)
    {
        if (!state)
        {
            return;
        }

        gui::rebuildMainWaveforms(state);
        resetSampleValueUnderMouseCursor(state);
        gui::Waveform::updateAllSamplePoints(state);
        gui::Waveform::setAllWaveformsDirty(state);
        gui::requestMainViewRefresh(state);
    }

    inline void closeCurrentDocument(cupuacu::State *state,
                                     const bool shouldPersistState = true)
    {
        if (!state)
        {
            return;
        }

        detail::discardAutosaveSnapshot(state->getActiveDocumentSession());
        prepareForDocumentTransition(state);

        auto &session = state->getActiveDocumentSession();
        detail::discardUndoStore(session);
        session.clearCurrentFile();
        session.document.initialize(cupuacu::SampleFormat::Unknown, 0, 0, 0);
        session.selection.reset();
        session.cursor = 0;
        session.syncSelectionAndCursorToDocumentLength();

        refreshDocumentUi(state);
        setMainWindowTitle(state, kUntitledDocumentTitle);
        if (shouldPersistState)
        {
            persistSessionState(state);
        }
    }

    inline void createNewDocument(cupuacu::State *state, const int sampleRate,
                                  const cupuacu::SampleFormat format,
                                  const int channels = 2,
                                  const bool shouldPersistState = true)
    {
        if (!state)
        {
            return;
        }

        detail::discardAutosaveSnapshot(state->getActiveDocumentSession());
        prepareForDocumentTransition(state);

        auto &session = state->getActiveDocumentSession();
        detail::discardUndoStore(session);
        session.clearCurrentFile();
        session.document.initialize(format, sampleRate, channels, 0);
        session.selection.reset();
        session.cursor = 0;
        session.syncSelectionAndCursorToDocumentLength();

        refreshDocumentUi(state);
        setMainWindowTitle(state, kUntitledDocumentTitle);
        if (shouldPersistState)
        {
            persistSessionState(state);
        }
    }

    inline bool createNewDocumentInNewTab(cupuacu::State *state,
                                          const int sampleRate,
                                          const cupuacu::SampleFormat format,
                                          const int channels = 2,
                                          const bool shouldPersistState = true)
    {
        if (!prepareTabForOpenedDocument(state))
        {
            return false;
        }

        createNewDocument(state, sampleRate, format, channels,
                          shouldPersistState);
        return true;
    }
} // namespace cupuacu::actions
