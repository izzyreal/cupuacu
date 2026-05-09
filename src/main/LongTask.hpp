#pragma once

#include "State.hpp"

#include <optional>
#include <stdexcept>
#include <string>

namespace cupuacu
{
    class LongTaskCanceledError : public std::runtime_error
    {
    public:
        LongTaskCanceledError()
            : std::runtime_error("Operation canceled")
        {
        }
    };

    void setLongTask(State *state, std::string title, std::string detail = {},
                     std::optional<double> progress = std::nullopt,
                     bool renderNow = true, bool cancellable = false);
    void updateLongTask(State *state, std::string detail = {},
                        std::optional<double> progress = std::nullopt,
                        bool renderNow = true);
    void updateLongTaskOverlayOnly(
        State *state, std::string detail = {},
        std::optional<double> progress = std::nullopt,
        bool renderNow = true);
    void renderLongTaskOverlayNow(State *state);
    void clearLongTask(State *state, bool renderNow = true);
    void requestLongTaskCancel(State *state);
    [[nodiscard]] bool isLongTaskCancelRequested(const State *state);
    [[nodiscard]] bool isLongTaskCancellable(const State *state);
    inline void throwIfLongTaskCanceled(const State *state)
    {
        if (isLongTaskCancelRequested(state))
        {
            throw LongTaskCanceledError{};
        }
    }

    class LongTaskScope
    {
    public:
        LongTaskScope(State *stateToUse, std::string title,
                      std::string detail = {},
                      std::optional<double> progress = std::nullopt,
                      bool renderNow = true, bool cancellable = false);
        ~LongTaskScope();

        LongTaskScope(const LongTaskScope &) = delete;
        LongTaskScope &operator=(const LongTaskScope &) = delete;

    private:
        State *state = nullptr;
        State::LongTaskStatus previousStatus;
        bool renderOnEnd = true;
    };
} // namespace cupuacu
