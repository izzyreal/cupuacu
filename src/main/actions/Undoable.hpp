#pragma once

#include "../State.hpp"
#include "../file/OverwritePreservationMutation.hpp"

#include <functional>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>

namespace cupuacu::actions
{
    class Undoable
    {
    protected:
        cupuacu::State *state;

    public:
        explicit Undoable(cupuacu::State *stateToUse) : state(stateToUse) {}
        virtual ~Undoable() = default;

        std::function<void()> updateGui = [] {};

        virtual void redo() = 0;
        virtual void undo() = 0;

        virtual std::string getRedoDescription() = 0;
        virtual std::string getUndoDescription() = 0;

        [[nodiscard]] virtual cupuacu::file::OverwritePreservationMutation
        overwritePreservationMutation() const = 0;

        [[nodiscard]] virtual bool canPersistForRestart() const
        {
            return false;
        }

        [[nodiscard]] virtual std::optional<nlohmann::json>
        serializeForRestart() const
        {
            return std::nullopt;
        }
    };
} // namespace cupuacu::actions
