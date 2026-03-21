#pragma once

#include "DocumentSession.hpp"
#include "gui/EditorViewState.hpp"

#include <deque>
#include <memory>
#include <string>

namespace cupuacu
{
    namespace actions
    {
        class Undoable;
    }

    struct DocumentTab
    {
        std::string title;
        DocumentSession session;
        gui::EditorViewState viewState{};
        std::deque<std::shared_ptr<actions::Undoable>> undoables;
        std::deque<std::shared_ptr<actions::Undoable>> redoables;
    };
} // namespace cupuacu
