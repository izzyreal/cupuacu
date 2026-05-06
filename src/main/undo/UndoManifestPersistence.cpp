#include "UndoManifestPersistence.hpp"

#include "../Logger.hpp"
#include "../actions/audio/Copy.hpp"
#include "../actions/audio/Cut.hpp"
#include "../actions/audio/Paste.hpp"
#include "../actions/audio/RecordEdit.hpp"
#include "../actions/audio/SetSampleValue.hpp"
#include "../actions/audio/Trim.hpp"
#include "../actions/markers/EditCommands.hpp"
#include "../effects/AmplifyEnvelopeEffect.hpp"
#include "../effects/AmplifyFadeEffect.hpp"
#include "../effects/DynamicsEffect.hpp"
#include "../effects/MakeSilentEffect.hpp"
#include "../effects/RemoveSilenceEffect.hpp"
#include "../effects/ReverseEffect.hpp"
#include "../file/FileIo.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <set>

namespace cupuacu::undo
{
    namespace
    {
        constexpr int kFormatVersion = 1;
        constexpr std::uint64_t kMaxRestartUndoStoreBytes =
            512ull * 1024ull * 1024ull;

        undo::UndoStore::PayloadHandle handleFromString(const std::string &path)
        {
            return {.path = path};
        }

        actions::audio::Trim::ViewSnapshot
        viewFromJson(const nlohmann::json &json)
        {
            return {
                .samplesPerPixel = json.value("samplesPerPixel", 1.0),
                .verticalZoom = json.value("verticalZoom", 1.0),
                .sampleOffset = json.value("sampleOffset", int64_t{0}),
            };
        }

        std::vector<effects::SilenceRange>
        silenceRunsFromJson(const nlohmann::json &json)
        {
            std::vector<effects::SilenceRange> runs;
            for (const auto &entry : json)
            {
                runs.push_back({
                    .startFrame = entry.value("startFrame", int64_t{0}),
                    .frameCount = entry.value("frameCount", int64_t{0}),
                });
            }
            return runs;
        }

        effects::AmplifyEnvelopeSettings
        amplifyEnvelopeSettingsFromJson(const nlohmann::json &json)
        {
            effects::AmplifyEnvelopeSettings settings{};
            settings.points.clear();
            if (json.contains("points") && json.at("points").is_array())
            {
                for (const auto &point : json.at("points"))
                {
                    settings.points.push_back({
                        .position = point.value("position", 0.0),
                        .percent = point.value("percent", 100.0),
                    });
                }
            }
            settings.snapEnabled = json.value("snapEnabled", false);
            settings.fadeLengthMs = json.value("fadeLengthMs", 100.0);
            return settings;
        }

        std::vector<int64_t> targetChannelsFromJson(const nlohmann::json &json)
        {
            std::vector<int64_t> channels;
            for (const auto &entry : json)
            {
                channels.push_back(entry.get<int64_t>());
            }
            return channels;
        }

        bool pathExists(const std::string &path)
        {
            return !path.empty() && std::filesystem::exists(path);
        }

        bool payloadPathsExist(
            const std::initializer_list<std::string> &paths)
        {
            for (const auto &path : paths)
            {
                if (!pathExists(path))
                {
                    return false;
                }
            }
            return true;
        }

        using RestoreFn =
            std::function<std::shared_ptr<actions::Undoable>(
                State *, int, const nlohmann::json &)>;

        const std::map<std::string, RestoreFn> &restoreRegistry()
        {
            static const std::map<std::string, RestoreFn> registry{
                {"cut",
                 [](State *state, int, const nlohmann::json &json)
                     -> std::shared_ptr<actions::Undoable>
                 {
                     const auto removedHandlePath =
                         json.value("removedHandle", std::string{});
                     if (!payloadPathsExist({removedHandlePath}))
                     {
                         return std::shared_ptr<actions::Undoable>{};
                     }
                     return std::make_shared<actions::audio::Cut>(
                         state, json.value("startFrame", int64_t{0}),
                         json.value("frameCount", int64_t{0}),
                         undo::UndoStore::SegmentHandle{
                             handleFromString(removedHandlePath).path},
                         json.value("oldSelectionStart", 0.0),
                         json.value("oldSelectionEnd", 0.0),
                         json.value("oldCursorPos", int64_t{0}));
                 }},
                {"paste",
                 [](State *state, int, const nlohmann::json &json)
                     -> std::shared_ptr<actions::Undoable>
                 {
                     const auto insertedHandlePath =
                         json.value("insertedHandle", std::string{});
                     const auto overwrittenHandlePath =
                         json.value("overwrittenHandle", std::string{});
                     const auto overwrittenFrameCount =
                         json.value("overwrittenFrameCount", int64_t{0});
                     if (!payloadPathsExist({insertedHandlePath}) ||
                         (overwrittenFrameCount > 0 &&
                          !payloadPathsExist({overwrittenHandlePath})))
                     {
                         return std::shared_ptr<actions::Undoable>{};
                     }
                     return std::make_shared<actions::audio::Paste>(
                         state, json.value("startFrame", int64_t{0}),
                         json.value("endFrame", int64_t{-1}),
                         json.value("insertedFrameCount", int64_t{0}),
                         overwrittenFrameCount,
                         undo::UndoStore::SegmentHandle{
                             handleFromString(insertedHandlePath).path},
                         undo::UndoStore::SegmentHandle{
                             handleFromString(overwrittenHandlePath).path},
                         json.value("hadOldSelection", false),
                         json.value("oldSelectionStart", 0.0),
                         json.value("oldSelectionEnd", 0.0),
                         json.value("oldCursorPos", int64_t{0}));
                 }},
                {"make-silent",
                 [](State *state, int, const nlohmann::json &json)
                     -> std::shared_ptr<actions::Undoable>
                 {
                     const auto originalHandlePath =
                         json.value("originalHandle", std::string{});
                     if (!payloadPathsExist({originalHandlePath}))
                     {
                         return std::shared_ptr<actions::Undoable>{};
                     }
                     return std::make_shared<effects::MakeSilentUndoable>(
                         state, json.value("startFrame", int64_t{0}),
                         json.value("frameCount", int64_t{0}),
                         targetChannelsFromJson(
                             json.value("targetChannels",
                                        nlohmann::json::array())),
                         undo::UndoStore::SegmentHandle{
                             handleFromString(originalHandlePath).path},
                         json.value("hadOldSelection", false),
                         json.value("oldSelectionStart", 0.0),
                         json.value("oldSelectionEnd", 0.0),
                         json.value("oldCursorPos", int64_t{0}));
                 }},
                {"trim",
                 [](State *state, int, const nlohmann::json &json)
                     -> std::shared_ptr<actions::Undoable>
                 {
                     const auto beforeCount = json.value("beforeCount", int64_t{0});
                     const auto afterCount = json.value("afterCount", int64_t{0});
                     const auto beforeHandlePath =
                         json.value("beforeHandle", std::string{});
                     const auto afterHandlePath =
                         json.value("afterHandle", std::string{});
                     if ((beforeCount > 0 && !payloadPathsExist({beforeHandlePath})) ||
                         (afterCount > 0 && !payloadPathsExist({afterHandlePath})))
                     {
                         return std::shared_ptr<actions::Undoable>{};
                     }
                     return std::make_shared<actions::audio::Trim>(
                         state, json.value("startFrame", int64_t{0}),
                         json.value("length", int64_t{0}),
                         beforeCount,
                         json.value("middleCount", int64_t{0}),
                         afterCount,
                         undo::UndoStore::SegmentHandle{
                             handleFromString(beforeHandlePath).path},
                         undo::UndoStore::SegmentHandle{
                             handleFromString(afterHandlePath).path},
                         viewFromJson(json.at("preTrimView")),
                         viewFromJson(json.at("postTrimView")),
                         json.value("hasPostTrimView", false));
                 }},
                {"record-edit",
                 [](State *state, int, const nlohmann::json &json)
                     -> std::shared_ptr<actions::Undoable>
                 {
                     const auto overwrittenHandlePath =
                         json.value("overwrittenOldSamplesHandle", std::string{});
                     const auto recordedHandlePath =
                         json.value("recordedSamplesHandle", std::string{});
                     if (!payloadPathsExist(
                             {overwrittenHandlePath, recordedHandlePath}))
                     {
                         return std::shared_ptr<actions::Undoable>{};
                     }
                     actions::audio::RecordEditData data{};
                     data.startFrame = json.value("startFrame", int64_t{0});
                     data.endFrame = json.value("endFrame", int64_t{0});
                     data.oldFrameCount = json.value("oldFrameCount", int64_t{0});
                     data.oldChannelCount = json.value("oldChannelCount", 0);
                     data.targetChannelCount =
                         json.value("targetChannelCount", 0);
                     data.oldSampleRate = json.value("oldSampleRate", 0);
                     data.newSampleRate = json.value("newSampleRate", 0);
                     data.oldFormat = static_cast<SampleFormat>(
                         json.value("oldFormat",
                                    static_cast<int>(SampleFormat::Unknown)));
                     data.newFormat = static_cast<SampleFormat>(
                         json.value("newFormat",
                                    static_cast<int>(SampleFormat::Unknown)));
                     data.hadOldSelection = json.value("hadOldSelection", false);
                     data.hadNewSelection = json.value("hadNewSelection", false);
                     data.oldSelectionStart =
                         json.value("oldSelectionStart", 0.0);
                     data.oldSelectionEnd = json.value("oldSelectionEnd", 0.0);
                     data.newSelectionStart =
                         json.value("newSelectionStart", 0.0);
                     data.newSelectionEnd = json.value("newSelectionEnd", 0.0);
                     data.oldCursor = json.value("oldCursor", int64_t{0});
                     data.newCursor = json.value("newCursor", int64_t{0});
                     return std::make_shared<actions::audio::RecordEdit>(
                         state, data,
                         undo::UndoStore::SampleMatrixHandle{
                             handleFromString(overwrittenHandlePath).path},
                         undo::UndoStore::SampleMatrixHandle{
                             handleFromString(recordedHandlePath).path});
                 }},
                {"reverse",
                 [](State *state, int tabIndex, const nlohmann::json &json)
                     -> std::shared_ptr<actions::Undoable>
                 {
                     const auto oldHandlePath =
                         json.value("oldSamplesHandle", std::string{});
                     const auto newHandlePath =
                         json.value("newSamplesHandle", std::string{});
                     if (!payloadPathsExist({oldHandlePath, newHandlePath}))
                     {
                         return std::shared_ptr<actions::Undoable>{};
                     }
                     return std::make_shared<effects::ReverseUndoable>(
                         state, tabIndex, json.value("startFrame", int64_t{0}),
                         json.value("frameCount", int64_t{0}),
                         targetChannelsFromJson(json.at("targetChannels")),
                         undo::UndoStore::SampleMatrixHandle{
                             handleFromString(oldHandlePath).path},
                         undo::UndoStore::SampleMatrixHandle{
                             handleFromString(newHandlePath).path});
                 }},
                {"amplify-fade",
                 [](State *state, int tabIndex, const nlohmann::json &json)
                     -> std::shared_ptr<actions::Undoable>
                 {
                     const auto oldHandlePath =
                         json.value("oldSamplesHandle", std::string{});
                     const auto newHandlePath =
                         json.value("newSamplesHandle", std::string{});
                     if (!payloadPathsExist({oldHandlePath, newHandlePath}))
                     {
                         return std::shared_ptr<actions::Undoable>{};
                     }
                     effects::AmplifyFadeSettings settings{};
                     const auto &settingsJson = json.at("settings");
                     settings.startPercent =
                         settingsJson.value("startPercent", 100.0);
                     settings.endPercent =
                         settingsJson.value("endPercent", 100.0);
                     settings.curveIndex = settingsJson.value("curveIndex", 0);
                     settings.lockEnabled =
                         settingsJson.value("lockEnabled", false);
                     return std::make_shared<effects::AmplifyFadeUndoable>(
                         state, tabIndex, settings,
                         json.value("startFrame", int64_t{0}),
                         json.value("frameCount", int64_t{0}),
                         targetChannelsFromJson(json.at("targetChannels")),
                         undo::UndoStore::SampleMatrixHandle{
                             handleFromString(oldHandlePath).path},
                         undo::UndoStore::SampleMatrixHandle{
                             handleFromString(newHandlePath).path});
                 }},
                {"dynamics",
                 [](State *state, int tabIndex, const nlohmann::json &json)
                     -> std::shared_ptr<actions::Undoable>
                 {
                     const auto oldHandlePath =
                         json.value("oldSamplesHandle", std::string{});
                     const auto newHandlePath =
                         json.value("newSamplesHandle", std::string{});
                     if (!payloadPathsExist({oldHandlePath, newHandlePath}))
                     {
                         return std::shared_ptr<actions::Undoable>{};
                     }
                     effects::DynamicsSettings settings{};
                     const auto &settingsJson = json.at("settings");
                     settings.thresholdPercent =
                         settingsJson.value("thresholdPercent", 50.0);
                     settings.ratioIndex = settingsJson.value("ratioIndex", 1);
                     return std::make_shared<effects::DynamicsUndoable>(
                         state, tabIndex, settings,
                         json.value("startFrame", int64_t{0}),
                         json.value("frameCount", int64_t{0}),
                         targetChannelsFromJson(json.at("targetChannels")),
                         undo::UndoStore::SampleMatrixHandle{
                             handleFromString(oldHandlePath).path},
                         undo::UndoStore::SampleMatrixHandle{
                             handleFromString(newHandlePath).path});
                 }},
                {"amplify-envelope",
                 [](State *state, int tabIndex, const nlohmann::json &json)
                     -> std::shared_ptr<actions::Undoable>
                 {
                     const auto oldHandlePath =
                         json.value("oldSamplesHandle", std::string{});
                     const auto newHandlePath =
                         json.value("newSamplesHandle", std::string{});
                     if (!payloadPathsExist({oldHandlePath, newHandlePath}))
                     {
                         return std::shared_ptr<actions::Undoable>{};
                     }
                     return std::make_shared<effects::AmplifyEnvelopeUndoable>(
                         state, tabIndex,
                         amplifyEnvelopeSettingsFromJson(json.at("settings")),
                         json.value("startFrame", int64_t{0}),
                         json.value("frameCount", int64_t{0}),
                         targetChannelsFromJson(json.at("targetChannels")),
                         undo::UndoStore::SampleMatrixHandle{
                             handleFromString(oldHandlePath).path},
                         undo::UndoStore::SampleMatrixHandle{
                             handleFromString(newHandlePath).path});
                 }},
                {"remove-silence-duration",
                 [](State *state, int tabIndex, const nlohmann::json &json)
                     -> std::shared_ptr<actions::Undoable>
                 {
                     const auto removedHandlePath =
                         json.value("removedSamplesHandle", std::string{});
                     if (!payloadPathsExist({removedHandlePath}))
                     {
                         return std::shared_ptr<actions::Undoable>{};
                     }
                     return std::make_shared<effects::RemoveSilenceUndoable>(
                         state, tabIndex, silenceRunsFromJson(json.at("runs")),
                         json.value("relevantStart", int64_t{0}),
                         json.value("originalRelevantLength", int64_t{0}),
                         undo::UndoStore::SampleCubeHandle{
                             handleFromString(removedHandlePath).path},
                         json.value("originalCursor", int64_t{0}),
                         json.value("hadSelection", false));
                 }},
                {"remove-silence-compact",
                 [](State *state, int tabIndex, const nlohmann::json &json)
                     -> std::shared_ptr<actions::Undoable>
                 {
                     const auto oldHandlePath =
                         json.value("oldSamplesHandle", std::string{});
                     const auto newHandlePath =
                         json.value("newSamplesHandle", std::string{});
                     if (!payloadPathsExist({oldHandlePath, newHandlePath}))
                     {
                         return std::shared_ptr<actions::Undoable>{};
                     }
                     return std::make_shared<
                         effects::RemoveSilenceChannelCompactUndoable>(
                         state, tabIndex,
                         targetChannelsFromJson(json.at("targetChannels")),
                         silenceRunsFromJson(json.at("runs")),
                         json.value("startFrame", int64_t{0}),
                         json.value("frameCount", int64_t{0}),
                         undo::UndoStore::SampleMatrixHandle{
                             handleFromString(oldHandlePath).path},
                         undo::UndoStore::SampleMatrixHandle{
                             handleFromString(newHandlePath).path});
                 }},
                {"set-sample-value",
                 [](State *state, int, const nlohmann::json &json)
                     -> std::shared_ptr<actions::Undoable>
                 {
                     return std::make_shared<actions::audio::SetSampleValue>(
                         state, json.value("channel", uint32_t{0}),
                         json.value("sampleIndex", int64_t{0}),
                         json.value("oldValue", 0.0f),
                         json.value("newValue", 0.0f));
                 }},
                {"set-marker-state",
                 [](State *state, int, const nlohmann::json &json)
                     -> std::shared_ptr<actions::Undoable>
                 {
                     if (!json.contains("oldState") || !json.contains("newState"))
                     {
                         return {};
                     }
                     const auto oldState =
                         actions::markers::markerSnapshotFromJson(
                             json.at("oldState"));
                     const auto newState =
                         actions::markers::markerSnapshotFromJson(
                             json.at("newState"));
                     if (!oldState.has_value() || !newState.has_value())
                     {
                         return {};
                     }
                     return std::make_shared<actions::markers::SetMarkerState>(
                         state, *oldState, *newState,
                         json.value("description", std::string{"Edit marker"}));
                 }},
                {"copy",
                 [](State *state, int, const nlohmann::json &json)
                     -> std::shared_ptr<actions::Undoable>
                 {
                     auto undoable = std::make_shared<actions::audio::Copy>(
                         state, json.value("startFrame", int64_t{0}),
                         json.value("numFrames", int64_t{0}),
                         json.value("hadOldSelection", false),
                         json.value("oldSel1", 0.0), json.value("oldSel2", 0.0),
                         json.value("oldCursorPos", int64_t{0}));
                     return undoable;
                 }},
            };
            return registry;
        }

        std::shared_ptr<actions::Undoable>
        restoreUndoable(State *state, const int tabIndex, const nlohmann::json &json)
        {
            const auto kind = json.value("kind", std::string{});
            const auto &registry = restoreRegistry();
            const auto it = registry.find(kind);
            if (it == registry.end())
            {
                return nullptr;
            }
            return it->second(state, tabIndex, json);
        }
    } // namespace

    std::uint64_t maxRestartUndoStoreBytes()
    {
        return kMaxRestartUndoStoreBytes;
    }

    bool shouldPersistUndoStoreForRestart(const UndoStore::Stats &stats,
                                          const std::uint64_t maxBytes)
    {
        return maxBytes == 0 || stats.totalBytes <= maxBytes;
    }

    std::string describeUndoStoreStats(const UndoStore::Stats &stats)
    {
        std::ostringstream output;
        output << stats.fileCount << " file(s), " << stats.totalBytes << " byte(s)";
        return output.str();
    }

    std::filesystem::path
    manifestPathForStore(const std::filesystem::path &undoStorePath)
    {
        return undoStorePath / "manifest.json";
    }

    bool saveUndoManifest(const std::filesystem::path &manifestPath,
                          const cupuacu::DocumentTab &tab)
    {
        if (manifestPath.empty() ||
            (tab.undoables.empty() && tab.redoables.empty()))
        {
            return false;
        }

        const auto serializeEntries =
            [](const auto &undoables) -> std::optional<nlohmann::json>
        {
            nlohmann::json entries = nlohmann::json::array();
            for (const auto &undoable : undoables)
            {
                if (!undoable || !undoable->canPersistForRestart())
                {
                    return std::nullopt;
                }
                const auto entry = undoable->serializeForRestart();
                if (!entry.has_value() || entry->is_null() || entry->empty())
                {
                    return std::nullopt;
                }
                entries.push_back(*entry);
            }
            return entries;
        };

        const auto undoEntries = serializeEntries(tab.undoables);
        const auto redoEntries = serializeEntries(tab.redoables);
        if (!undoEntries.has_value() || !redoEntries.has_value())
        {
            return false;
        }

        const nlohmann::json json{
            {"version", kFormatVersion},
            {"entries", std::move(*undoEntries)},
            {"redoEntries", std::move(*redoEntries)},
        };

        try
        {
            cupuacu::file::writeFileAtomically(
                manifestPath,
                [&](const std::filesystem::path &temporaryPath)
                {
                    std::ofstream output(temporaryPath);
                    output << json.dump(2) << '\n';
                });
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    bool restoreUndoManifest(cupuacu::State *state, const int tabIndex,
                             const std::filesystem::path &undoStorePath)
    {
        if (!state || tabIndex < 0 || tabIndex >= static_cast<int>(state->tabs.size()) ||
            undoStorePath.empty() || !std::filesystem::exists(undoStorePath))
        {
            return false;
        }

        const auto manifestPath = manifestPathForStore(undoStorePath);
        if (!std::filesystem::exists(manifestPath))
        {
            return false;
        }

        std::ifstream input(manifestPath);
        if (!input.is_open())
        {
            return false;
        }

        nlohmann::json json;
        try
        {
            input >> json;
        }
        catch (...)
        {
            return false;
        }

        if (!json.is_object() || json.value("version", 0) != kFormatVersion ||
            !json.contains("entries") || !json.at("entries").is_array())
        {
            return false;
        }
        if (json.contains("redoEntries") && !json.at("redoEntries").is_array())
        {
            return false;
        }

        auto &tab = state->tabs[static_cast<std::size_t>(tabIndex)];
        tab.session.undoStore.attach(undoStorePath);
        tab.undoables.clear();
        tab.redoables.clear();

        for (const auto &entry : json.at("entries"))
        {
            auto undoable = restoreUndoable(state, tabIndex, entry);
            if (!undoable)
            {
                tab.undoables.clear();
                tab.redoables.clear();
                cupuacu::logging::warn(
                    "Failed to restore restart undo history from " +
                    undoStorePath.string());
                return false;
            }
            tab.undoables.push_back(std::move(undoable));
        }

        if (json.contains("redoEntries"))
        {
            for (const auto &entry : json.at("redoEntries"))
            {
                auto redoable = restoreUndoable(state, tabIndex, entry);
                if (!redoable)
                {
                    tab.undoables.clear();
                    tab.redoables.clear();
                    cupuacu::logging::warn(
                        "Failed to restore restart redo history from " +
                        undoStorePath.string());
                    return false;
                }
                tab.redoables.push_back(std::move(redoable));
            }
        }

        cupuacu::logging::info(
            "Restored restart undo history from " + undoStorePath.string() +
            " (" + describeUndoStoreStats(tab.session.undoStore.stats()) + ")");
        return !tab.undoables.empty() || !tab.redoables.empty();
    }

    void pruneUndoStores(const std::filesystem::path &undoRoot,
                         const cupuacu::persistence::PersistedSessionState &state)
    {
        if (undoRoot.empty() || !std::filesystem::exists(undoRoot))
        {
            return;
        }

        std::set<std::filesystem::path> keep;
        for (const auto &documentState : state.openDocuments)
        {
            if (!documentState.undoStorePath.empty())
            {
                keep.insert(std::filesystem::path(documentState.undoStorePath)
                                .lexically_normal());
            }
        }

        std::error_code ec;
        std::uint64_t removedCount = 0;
        for (const auto &entry : std::filesystem::directory_iterator(undoRoot, ec))
        {
            if (ec)
            {
                return;
            }
            const auto normalized = entry.path().lexically_normal();
            if (keep.find(normalized) == keep.end())
            {
                std::filesystem::remove_all(entry.path(), ec);
                if (!ec)
                {
                    ++removedCount;
                }
                ec.clear();
            }
        }
        if (removedCount > 0)
        {
            cupuacu::logging::info(
                "Pruned " + std::to_string(removedCount) +
                " stale undo store(s) from " + undoRoot.string());
        }
    }
} // namespace cupuacu::undo
