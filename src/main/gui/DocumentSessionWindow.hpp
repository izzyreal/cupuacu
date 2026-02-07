#pragma once

#include "../DocumentSession.hpp"

#include "EditorViewState.hpp"
#include "Window.hpp"

#include <memory>
#include <string>

namespace cupuacu
{
    struct State;
}

namespace cupuacu::gui
{
    class DocumentSessionWindow
    {
    public:
        DocumentSessionWindow(State *state, DocumentSession *session,
                              const std::string &title, int width, int height,
                              Uint32 flags);

        Window *getWindow() const
        {
            return window.get();
        }

        EditorViewState &getViewState()
        {
            return viewState;
        }

        const EditorViewState &getViewState() const
        {
            return viewState;
        }

        DocumentSession *getDocumentSession() const
        {
            return documentSession;
        }

    private:
        DocumentSession *documentSession = nullptr;
        EditorViewState viewState{};
        std::unique_ptr<Window> window;
    };
} // namespace cupuacu::gui
