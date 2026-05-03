#pragma once

#include "DocumentSession.hpp"
#include "gui/EditorViewState.hpp"

#include <deque>
#include <cstdint>
#include <memory>
#include <string>

namespace cupuacu
{
    namespace detail
    {
        inline uint64_t nextDocumentTabId()
        {
            static uint64_t nextId = 1;
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
        std::string title;
        DocumentSession session;
        gui::EditorViewState viewState{};
        std::deque<std::shared_ptr<actions::Undoable>> undoables;
        std::deque<std::shared_ptr<actions::Undoable>> redoables;
    };
} // namespace cupuacu
