#include "State.hpp"

#include "effects/AmplifyEnvelopeEffect.hpp"
#include "effects/AmplifyFadeEffect.hpp"
#include "effects/DynamicsEffect.hpp"
#include "effects/RemoveSilenceEffect.hpp"

#include "gui/Waveform.hpp"
#include "gui/ExportAudioDialogWindow.hpp"
#include "gui/GenerateSilenceDialogWindow.hpp"
#include "gui/MarkerEditorDialogWindow.hpp"
#include "gui/NewFileDialogWindow.hpp"
#include "gui/OptionsWindow.hpp"
#include "actions/effects/BackgroundEffect.hpp"
#include "actions/io/BackgroundOpen.hpp"
#include "actions/io/BackgroundSave.hpp"
#include "actions/Undoable.hpp"
#include "actions/DocumentLifecycle.hpp"
#include "file/OverwritePreservation.hpp"
#include "file/OverwritePreservationMutation.hpp"

#include <chrono>
#include <cstdio>

int64_t getMaxSampleOffset(const cupuacu::State *state)
{
    if (!state->mainDocumentSessionWindow || state->waveforms.empty() ||
        state->getActiveDocumentSession().document.getFrameCount() == 0)
    {
        return 0;
    }
    const auto &viewState = state->getActiveViewState();

    const double waveformWidth =
        static_cast<double>(state->waveforms.front()->getWidth());
    const int64_t visibleSampleCount = static_cast<int64_t>(
        std::ceil(waveformWidth * viewState.samplesPerPixel));
    const int64_t frameCount =
        state->getActiveDocumentSession().document.getFrameCount();
    const int64_t maxOffset = frameCount - visibleSampleCount;
    return std::max<int64_t>(0, maxOffset);
}

void cupuacu::destroyAmplifyEnvelopeDialog(
    effects::AmplifyEnvelopeDialog *dialog)
{
    delete dialog;
}

void cupuacu::destroyAmplifyFadeDialog(effects::AmplifyFadeDialog *dialog)
{
    delete dialog;
}

void cupuacu::destroyDynamicsDialog(effects::DynamicsDialog *dialog)
{
    delete dialog;
}

void cupuacu::destroyRemoveSilenceDialog(effects::RemoveSilenceDialog *dialog)
{
    delete dialog;
}

void cupuacu::destroyOptionsWindow(gui::OptionsWindow *window)
{
    delete window;
}

void cupuacu::destroyNewFileDialogWindow(gui::NewFileDialogWindow *dialog)
{
    delete dialog;
}

void cupuacu::destroyGenerateSilenceDialogWindow(
    gui::GenerateSilenceDialogWindow *dialog)
{
    delete dialog;
}

void cupuacu::destroyExportAudioDialogWindow(
    gui::ExportAudioDialogWindow *dialog)
{
    delete dialog;
}

void cupuacu::destroyMarkerEditorDialogWindow(
    gui::MarkerEditorDialogWindow *dialog)
{
    delete dialog;
}

void cupuacu::destroyBackgroundOpenJob(actions::io::BackgroundOpenJob *job)
{
    delete job;
}

void cupuacu::destroyBackgroundSaveJob(actions::io::BackgroundSaveJob *job)
{
    delete job;
}

void cupuacu::destroyBackgroundAutosaveJob(
    actions::io::BackgroundAutosaveJob *job)
{
    delete job;
}

void cupuacu::destroyBackgroundEffectJob(
    actions::effects::BackgroundEffectJob *job)
{
    delete job;
}

cupuacu::State::~State() = default;

void cupuacu::State::addUndoableToTab(
    const int tabIndex, std::shared_ptr<cupuacu::actions::Undoable> undoable)
{
    if (tabIndex < 0 || tabIndex >= static_cast<int>(tabs.size()))
    {
        return;
    }

    auto &tab = tabs[static_cast<std::size_t>(tabIndex)];
    tab.undoables.push_back(std::move(undoable));
    tab.redoables.clear();
}

void cupuacu::State::addAndDoUndoableToTab(
    const int tabIndex, std::shared_ptr<cupuacu::actions::Undoable> undoable)
{
    if (tabIndex < 0 || tabIndex >= static_cast<int>(tabs.size()) || !undoable)
    {
        return;
    }

    addUndoableToTab(tabIndex, undoable);
    const auto operationStartedAt = std::chrono::steady_clock::now();
    const auto redoStartedAt = operationStartedAt;
    undoable->redo();
    const auto redoCompletedAt = std::chrono::steady_clock::now();

    auto &session = tabs[static_cast<std::size_t>(tabIndex)].session;
    const auto preservationMutationStartedAt =
        std::chrono::steady_clock::now();
    cupuacu::file::OverwritePreservationMutationHelper::applyToSession(
        session, undoable->overwritePreservationMutation());
    const auto preservationMutationCompletedAt =
        std::chrono::steady_clock::now();

    const auto refreshStartedAt = std::chrono::steady_clock::now();
    cupuacu::file::OverwritePreservation::refreshSession(this, tabIndex);
    const auto refreshCompletedAt = std::chrono::steady_clock::now();

    const auto guiUpdateStartedAt = std::chrono::steady_clock::now();
    if (tabIndex == activeTabIndex)
    {
        undoable->updateGui();
    }
    const auto guiUpdateCompletedAt = std::chrono::steady_clock::now();

    const auto autosaveStartedAt = std::chrono::steady_clock::now();
    cupuacu::actions::autosaveDocumentAfterMutation(this, tabIndex);
    const auto autosaveCompletedAt = std::chrono::steady_clock::now();

    const auto toMilliseconds = [](const auto start, const auto end)
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
            .count();
    };

    std::printf(
        "[undoable] redo_ms=%lld preservation_mutation_ms=%lld "
        "preservation_refresh_ms=%lld gui_update_ms=%lld autosave_ms=%lld "
        "total_ms=%lld\n",
        static_cast<long long>(toMilliseconds(redoStartedAt, redoCompletedAt)),
        static_cast<long long>(toMilliseconds(preservationMutationStartedAt,
                                              preservationMutationCompletedAt)),
        static_cast<long long>(
            toMilliseconds(refreshStartedAt, refreshCompletedAt)),
        static_cast<long long>(
            toMilliseconds(guiUpdateStartedAt, guiUpdateCompletedAt)),
        static_cast<long long>(
            toMilliseconds(autosaveStartedAt, autosaveCompletedAt)),
        static_cast<long long>(
            toMilliseconds(operationStartedAt, autosaveCompletedAt)));
    std::fflush(stdout);
}

void cupuacu::State::addUndoable(
    std::shared_ptr<cupuacu::actions::Undoable> undoable)
{
    addUndoableToTab(activeTabIndex, std::move(undoable));
}

void cupuacu::State::addAndDoUndoable(
    std::shared_ptr<cupuacu::actions::Undoable> undoable)
{
    addAndDoUndoableToTab(activeTabIndex, std::move(undoable));
}

void cupuacu::State::undo()
{
    auto &undoables = getActiveUndoables();
    auto &redoables = getActiveRedoables();
    if (undoables.empty())
    {
        return;
    }

    auto undoable = undoables.back();
    undoables.pop_back();
    undoable->undo();
    cupuacu::file::OverwritePreservationMutationHelper::revertOnSession(
        getActiveDocumentSession(), undoable->overwritePreservationMutation());
    cupuacu::file::OverwritePreservation::refreshActiveSession(this);
    undoable->updateGui();
    redoables.push_back(undoable);
    cupuacu::actions::autosaveActiveDocumentAfterMutation(this);
}

void cupuacu::State::redo()
{
    auto &undoables = getActiveUndoables();
    auto &redoables = getActiveRedoables();
    if (redoables.empty())
    {
        return;
    }

    auto redoable = redoables.back();
    redoables.pop_back();
    redoable->redo();
    cupuacu::file::OverwritePreservationMutationHelper::applyToSession(
        getActiveDocumentSession(), redoable->overwritePreservationMutation());
    cupuacu::file::OverwritePreservation::refreshActiveSession(this);
    redoable->updateGui();
    undoables.push_back(redoable);
    cupuacu::actions::autosaveActiveDocumentAfterMutation(this);
}

std::string cupuacu::State::getUndoDescription()
{
    if (!canUndo())
    {
        return "";
    }
    return getActiveUndoables().back()->getUndoDescription();
}
std::string cupuacu::State::getRedoDescription()
{
    if (!canRedo())
    {
        return "";
    }
    return getActiveRedoables().back()->getRedoDescription();
}
