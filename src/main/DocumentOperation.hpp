#pragma once
#include <cstdint>
#include <optional>
#include <string>
namespace cupuacu
{
    struct DocumentOperation
    {
        enum class Kind
        {
            Import,
            Effect,
            Edit,
            Save
        };
        Kind kind = Kind::Import;
        uint64_t id = 0;
        std::string title, detail;
        std::optional<double> progress;
        bool cancelRequested = false;
        bool blocksMutation() const
        {
            return kind != Kind::Save;
        }
    };
} // namespace cupuacu
