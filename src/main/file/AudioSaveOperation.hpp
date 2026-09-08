#pragma once

#include "../Document.hpp"
#include "AudioExport.hpp"
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace cupuacu::storage
{
    class AudioEditRevision;
    class AudioRevision;
}

namespace cupuacu::file
{
    // Capture before scheduling or invoking callbacks. Resident samples share
    // their immutable buffer; revision documents retain only shape and markers.
    struct AudioSaveSnapshot
    {
        Document document;
        std::shared_ptr<const storage::AudioEditRevision> revision;
        std::shared_ptr<const storage::AudioRevision> preservationSource;
    };

    struct AudioSaveRequest
    {
        std::filesystem::path outputPath, referencePath, workingRoot;
        AudioExportSettings settings;
        bool preserving = false;
    };

    // Blocking operation over pinned input, with no UI or session publication.
    // A progress callback may throw to cancel. Returns the owned output
    // container for revisions, or null for resident compatibility input.
    std::shared_ptr<const storage::AudioRevision> writeAudioSave(
        const AudioSaveSnapshot &, const AudioSaveRequest &,
        const std::function<void(const std::string &, std::optional<double>)> &progress = {});
}
