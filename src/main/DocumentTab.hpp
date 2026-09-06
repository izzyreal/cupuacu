#pragma once

#include "DocumentSession.hpp"
#include "DocumentOperation.hpp"
#include "gui/EditorViewState.hpp"

#include <deque>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

namespace cupuacu
{
    namespace detail
    {
        inline uint64_t nextDocumentTabId()
        {
            static std::atomic<uint64_t> nextId{1};
            return nextId++;
        }
    } // namespace detail

    namespace actions
    {
        class Undoable;
    }

    struct DocumentTab
    {
        uint64_t id = detail::nextDocumentTabId();
        uint64_t historyVersion = 0;
        std::optional<DocumentOperation> operation;
        std::string title;
        DocumentSession session;
        gui::EditorViewState viewState{};
        std::optional<file::AudioExportSettings> lastExportAudioDialogSettings;
        std::deque<std::shared_ptr<actions::Undoable>> undoables;
        std::deque<std::shared_ptr<actions::Undoable>> redoables;
    };
} // namespace cupuacu
