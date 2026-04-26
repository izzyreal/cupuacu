#pragma once

#include "../DocumentSession.hpp"

#include <filesystem>

namespace cupuacu::persistence
{
    bool saveDocumentAutosaveSnapshot(const std::filesystem::path &path,
                                      const cupuacu::DocumentSession &session);

    bool loadDocumentAutosaveSnapshot(const std::filesystem::path &path,
                                      cupuacu::DocumentSession &session);

    void removeDocumentAutosaveSnapshot(const std::filesystem::path &path);
} // namespace cupuacu::persistence
