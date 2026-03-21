#include "DocumentSessionWindow.hpp"

#include "../State.hpp"

using namespace cupuacu::gui;

DocumentSessionWindow::DocumentSessionWindow(State *state,
                                             DocumentSession *session,
                                             EditorViewState *viewStateToUse,
                                             const std::string &title,
                                             const int width, const int height,
                                             const Uint32 flags)
    : documentSession(session), viewState(viewStateToUse)
{
    window = std::make_unique<Window>(state, title, width, height, flags);
}
