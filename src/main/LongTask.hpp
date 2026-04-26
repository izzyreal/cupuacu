#pragma once

#include "State.hpp"

#include <optional>
#include <string>

namespace cupuacu
{
    void setLongTask(State *state, std::string title, std::string detail = {},
                     std::optional<double> progress = std::nullopt,
                     bool renderNow = true);
    void updateLongTask(State *state, std::string detail = {},
                        std::optional<double> progress = std::nullopt,
                        bool renderNow = true);
    void clearLongTask(State *state, bool renderNow = true);

    class LongTaskScope
    {
    public:
        LongTaskScope(State *stateToUse, std::string title,
                      std::string detail = {},
                      std::optional<double> progress = std::nullopt,
                      bool renderNow = true);
        ~LongTaskScope();

        LongTaskScope(const LongTaskScope &) = delete;
        LongTaskScope &operator=(const LongTaskScope &) = delete;

    private:
        State *state = nullptr;
        State::LongTaskStatus previousStatus;
        bool renderOnEnd = true;
    };
} // namespace cupuacu
