#pragma once

#include "Component.hpp"

namespace cupuacu::gui
{
    template <typename T>
    inline T *findComponentOfType(cupuacu::gui::Component *root)
    {
        if (!root)
        {
            return nullptr;
        }

        if (auto *typed = dynamic_cast<T *>(root))
        {
            return typed;
        }

        for (const auto &child : root->getChildren())
        {
            if (auto *found = findComponentOfType<T>(child.get()))
            {
                return found;
            }
        }

        return nullptr;
    }
} // namespace cupuacu::gui
