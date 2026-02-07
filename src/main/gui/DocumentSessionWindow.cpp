#include "DocumentSessionWindow.hpp"

#include "../State.hpp"

using namespace cupuacu::gui;

DocumentSessionWindow::DocumentSessionWindow(State *state,
                                             DocumentSession *session,
                                             const std::string &title,
                                             const int width, const int height,
                                             const Uint32 flags)
    : documentSession(session)
{
    window = std::make_unique<Window>(state, title, width, height, flags);
}
