#include "LongTask.hpp"

#include "State.hpp"
#include "gui/EventHandling.hpp"
#include "gui/LongTaskOverlay.hpp"
#include "gui/Window.hpp"

#include <SDL3/SDL.h>

namespace cupuacu
{
    namespace
    {
        void syncLongTaskOverlays(gui::Component *component)
        {
            if (!component)
            {
                return;
            }
            if (auto *overlay = dynamic_cast<gui::LongTaskOverlay *>(component))
            {
                overlay->syncToState();
            }
            for (const auto &child : component->getChildren())
            {
                syncLongTaskOverlays(child.get());
            }
        }

        bool markLongTaskOverlaysDirty(gui::Component *component)
        {
            if (!component)
            {
                return false;
            }

            bool found = false;
            if (auto *overlay = dynamic_cast<gui::LongTaskOverlay *>(component))
            {
                overlay->setDirty();
                found = true;
            }
            for (const auto &child : component->getChildren())
            {
                found = markLongTaskOverlaysDirty(child.get()) || found;
            }
            return found;
        }

        void markWindowRootsDirty(State *state)
        {
            if (!state)
            {
                return;
            }
            for (auto *window : state->windows)
            {
                if (!window || !window->isOpen() || !window->getRootComponent())
                {
                    continue;
                }
                window->getRootComponent()->setDirty();
            }
        }

        void refreshLongTaskUi(State *state, const bool renderNow)
        {
            if (!state)
            {
                return;
            }

            for (auto *window : state->windows)
            {
                if (!window || !window->isOpen() || !window->getRootComponent())
                {
                    continue;
                }
                syncLongTaskOverlays(window->getRootComponent());
                if (!markLongTaskOverlaysDirty(window->getRootComponent()))
                {
                    continue;
                }
                if (state->longTask.active)
                {
                    window->getRootComponent()->setDirty();
                }
                if (renderNow && state->mainWindowInitialFrameRendered)
                {
                    SDL_Event event{};
                    SDL_PumpEvents();
                    while (SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_EVENT_FIRST,
                                          SDL_EVENT_LAST) == 1)
                    {
                        (void)cupuacu::gui::handleAppEvent(state, &event);
                    }
                    window->renderFrame();
                }
            }
        }

        void notifyLongTaskObserver(State *state)
        {
            if (!state || !state->longTaskObserver)
            {
                return;
            }
            state->longTaskObserver(state->longTask);
        }
    } // namespace

    void setLongTask(State *state, std::string title, std::string detail,
                     std::optional<double> progress, const bool renderNow,
                     const bool cancellable)
    {
        if (!state)
        {
            return;
        }

        state->longTask.active = true;
        state->longTask.title = std::move(title);
        state->longTask.detail = std::move(detail);
        state->longTask.progress = progress;
        state->longTask.cancellable = cancellable;
        state->longTask.cancelRequested = false;
        notifyLongTaskObserver(state);
        refreshLongTaskUi(state, renderNow);
    }

    void updateLongTask(State *state, std::string detail,
                        std::optional<double> progress, const bool renderNow)
    {
        if (!state || !state->longTask.active)
        {
            return;
        }

        if (!detail.empty())
        {
            state->longTask.detail = std::move(detail);
        }
        state->longTask.progress = progress;
        notifyLongTaskObserver(state);
        refreshLongTaskUi(state, renderNow);
    }

    void updateLongTaskOverlayOnly(State *state, std::string detail,
                                   std::optional<double> progress,
                                   const bool renderNow)
    {
        if (!state || !state->longTask.active)
        {
            return;
        }

        if (!detail.empty())
        {
            state->longTask.detail = std::move(detail);
        }
        state->longTask.progress = progress;
        notifyLongTaskObserver(state);

        for (auto *window : state->windows)
        {
            if (!window || !window->isOpen() || !window->getRootComponent())
            {
                continue;
            }
            syncLongTaskOverlays(window->getRootComponent());
            if (!markLongTaskOverlaysDirty(window->getRootComponent()))
            {
                continue;
            }
            if (renderNow && state->mainWindowInitialFrameRendered)
            {
                SDL_Event event{};
                SDL_PumpEvents();
                while (SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_EVENT_FIRST,
                                      SDL_EVENT_LAST) == 1)
                {
                    (void)cupuacu::gui::handleAppEvent(state, &event);
                }
                window->renderOverlayFrame();
            }
        }
    }

    void renderLongTaskOverlayNow(State *state)
    {
        if (!state || !state->longTask.active || !state->mainWindowInitialFrameRendered)
        {
            return;
        }

        for (auto *window : state->windows)
        {
            if (!window || !window->isOpen() || !window->getRootComponent())
            {
                continue;
            }
            syncLongTaskOverlays(window->getRootComponent());
            if (!markLongTaskOverlaysDirty(window->getRootComponent()))
            {
                continue;
            }
            SDL_Event event{};
            SDL_PumpEvents();
            while (SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_EVENT_FIRST,
                                  SDL_EVENT_LAST) == 1)
            {
                (void)cupuacu::gui::handleAppEvent(state, &event);
            }
            window->renderOverlayFrame();
        }
    }

    void clearLongTask(State *state, const bool renderNow)
    {
        if (!state)
        {
            return;
        }

        markWindowRootsDirty(state);
        state->longTask = {};
        notifyLongTaskObserver(state);
        refreshLongTaskUi(state, renderNow);
    }

    void requestLongTaskCancel(State *state)
    {
        if (!state || !state->longTask.active || !state->longTask.cancellable)
        {
            return;
        }

        state->longTask.cancelRequested = true;
        notifyLongTaskObserver(state);
        refreshLongTaskUi(state, false);
    }

    bool isLongTaskCancelRequested(const State *state)
    {
        return state && state->longTask.active && state->longTask.cancelRequested;
    }

    bool isLongTaskCancellable(const State *state)
    {
        return state && state->longTask.active && state->longTask.cancellable;
    }

    LongTaskScope::LongTaskScope(State *stateToUse, std::string title,
                                 std::string detail,
                                 std::optional<double> progress,
                                 const bool renderNow,
                                 const bool cancellable)
        : state(stateToUse),
          previousStatus(stateToUse ? stateToUse->longTask
                                    : State::LongTaskStatus{}),
          renderOnEnd(renderNow)
    {
        setLongTask(state, std::move(title), std::move(detail), progress,
                    renderNow, cancellable);
    }

    LongTaskScope::~LongTaskScope()
    {
        if (!state)
        {
            return;
        }

        if (!previousStatus.active)
        {
            markWindowRootsDirty(state);
        }
        state->longTask = std::move(previousStatus);
        notifyLongTaskObserver(state);
        const bool shouldRenderOnEnd = renderOnEnd || !state->longTask.active;
        refreshLongTaskUi(state, shouldRenderOnEnd);
    }
} // namespace cupuacu
