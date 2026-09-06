#include "RevisionPersistence.hpp"
#include "../actions/audio/SetSampleValue.hpp"
#include "../undo/UndoManifestPersistence.hpp"
#include "../concurrency/DeferredRelease.hpp"
#include "../Logger.hpp"
#include <set>

namespace cupuacu::persistence
{
    namespace
    {
        using Json = nlohmann::json;
        Json markers(const std::vector<DocumentMarker> &values)
        {
            auto j = Json::array();
            for (const auto &m : values)
            {
                j.push_back({m.id, m.frame, m.label});
            }
            return j;
        }
        std::vector<DocumentMarker> markers(const Json &j)
        {
            std::vector<DocumentMarker> out;
            for (const auto &m : j)
            {
                out.push_back({m.at(0), m.at(1), m.at(2)});
            }
            return out;
        }
        Json view(const gui::EditorViewState &s)
        {
            return {s.samplesPerPixel, s.verticalZoom, s.sampleOffset,
                    int(s.selectedChannels)};
        }
        gui::EditorViewState view(const Json &j)
        {
            gui::EditorViewState s;
            s.samplesPerPixel = j.at(0);
            s.verticalZoom = j.at(1);
            s.sampleOffset = j.at(2);
            s.selectedChannels = SelectedChannels(j.at(3).get<int>());
            return s;
        }
        Json settings(const std::optional<file::AudioExportSettings> &s)
        {
            if (!s)
            {
                return nullptr;
            }
            Json j = {{"container", int(s->container)},
                      {"codec", int(s->codec)},
                      {"major", s->majorFormat},
                      {"subtype", s->subtype},
                      {"containerLabel", s->containerLabel},
                      {"codecLabel", s->codecLabel},
                      {"encodingLabel", s->encodingLabel},
                      {"extension", s->extension}};
            if (s->compressionLevel)
            {
                j["compression"] = *s->compressionLevel;
            }
            if (s->bitrateMode)
            {
                j["mode"] = *s->bitrateMode;
            }
            if (s->bitrateKbps)
            {
                j["kbps"] = *s->bitrateKbps;
            }
            return j;
        }
        std::optional<file::AudioExportSettings> settings(const Json &j)
        {
            if (j.is_null())
            {
                return {};
            }
            file::AudioExportSettings s;
            s.container =
                file::AudioExportContainer(j.at("container").get<int>());
            s.codec = file::AudioExportCodec(j.at("codec").get<int>());
            s.majorFormat = j.at("major");
            s.subtype = j.at("subtype");
            s.containerLabel = j.at("containerLabel");
            s.codecLabel = j.at("codecLabel");
            s.encodingLabel = j.at("encodingLabel");
            s.extension = j.at("extension");
            if (j.contains("compression"))
            {
                s.compressionLevel = j.at("compression").get<double>();
            }
            if (j.contains("mode"))
            {
                s.bitrateMode = j.at("mode").get<int>();
            }
            if (j.contains("kbps"))
            {
                s.bitrateKbps = j.at("kbps").get<int>();
            }
            if (!s.isValid())
            {
                throw std::runtime_error("Invalid recovered export settings");
            }
            return s;
        }
        Json saveState(storage::RevisionArchive &a,
                       const actions::audio::RevisionEditState &s)
        {
            return {{"root", a.save(s.audio)},
                    {"markers", markers(s.markers)},
                    {"cursor", s.cursor},
                    {"selection",
                     s.selection.isActive()
                         ? Json{s.selection.getStart(), s.selection.getEnd()}
                         : Json{}}};
        }
        actions::audio::RevisionEditState loadState(storage::RevisionArchive &a,
                                                    const Json &j)
        {
            actions::audio::RevisionEditState s;
            s.audio = a.load(j.at("root"));
            s.markers = markers(j.at("markers"));
            s.cursor = j.at("cursor");
            if (s.audio)
            {
                s.selection.setHighest(s.audio->shape().frames);
            }
            const auto &selected = j.at("selection");
            if (!selected.is_null())
            {
                s.selection.setValue1(selected.at(0));
                s.selection.setValue2(selected.at(1));
            }
            return s;
        }
    } // namespace
    std::shared_ptr<RevisionCheckpoint>
    RevisionPersistence::capture(const DocumentSession &s,
                                 const DocumentTab *tab)
    {
        auto cp = std::make_shared<RevisionCheckpoint>();
        cp->current = actions::audio::RevisionEditState::capture(s);
        if (!cp->current.audio)
        {
            throw std::invalid_argument(
                "Revision checkpoint requires revision audio");
        }
        cp->saved.audio = s.savedReadRevision;
        cp->saved.markers = s.savedRevisionMarkers;
        cp->preservation = s.preservationSource;
        cp->metadata = {
            {"file", s.currentFile},
            {"settings", settings(s.currentFileExportSettings)},
            {"requiresSaveAs", s.currentFileRequiresSaveAs},
            {"reference", s.preservationReferenceFile},
            {"referenceSettings",
             settings(s.preservationReferenceExportSettings)},
            {"preservationBroken", s.overwritePreservationBrokenByOperation},
            {"preservationReason", s.overwritePreservationBrokenReason},
            {"sourceId", s.document.getPreservationSourceId()}};
        auto captureHistory = [&](const auto &entries, auto &out)
        {
            for (const auto &entry : entries)
            {
                RevisionCheckpoint::History h;
                if (auto *edit = dynamic_cast<actions::audio::RevisionEdit *>(
                        entry.get()))
                {
                    h.before = edit->before;
                    h.after = edit->after;
                    if (edit->copiedAudio)
                    {
                        h.copied = edit->copiedAudio->getAudioRevision();
                    }
                    h.details = {{"kind", "revision"},
                                 {"name", edit->description},
                                 {"trim", edit->trimView},
                                 {"beforeView", view(edit->beforeView)},
                                 {"afterView", view(edit->afterView)},
                                 {"haveAfterView", edit->haveAfterView}};
                }
                else if (auto *point =
                             dynamic_cast<actions::audio::SetSampleValue *>(
                                 entry.get());
                         point && point->revisionBefore)
                {
                    h.before = *point->revisionBefore;
                    h.after = h.before;
                    h.after.audio = point->revisionAfter ? point->revisionAfter
                                                         : h.before.audio;
                    h.details = {
                        {"kind", "point"},
                        {"channel", point->channel},
                        {"sample", point->sampleIndex},
                        {"old", std::bit_cast<uint32_t>(point->oldValue)},
                        {"new", std::bit_cast<uint32_t>(point->newValue)},
                        {"appliedAfter",
                         point->appliedRevision == h.after.audio}};
                }
                else if (entry && entry->canPersistForRestart())
                {
                    auto json = entry->serializeForRestart();
                    if (!json || json->value("kind", std::string{}) !=
                                     "set-marker-state")
                    {
                        throw std::runtime_error(
                            "Restart history contains an unmigrated resident "
                            "audio command");
                    }
                    h.details = {{"kind", "legacy"}, {"entry", *json}};
                }
                else
                {
                    throw std::runtime_error(
                        "Unsupported restart history entry");
                }
                out.push_back(std::move(h));
            }
        };
        if (tab)
        {
            cp->metadata["view"] = view(tab->viewState);
            cp->metadata["historyVersion"] = tab->historyVersion;
            try
            {
                captureHistory(tab->undoables, cp->undo);
                captureHistory(tab->redoables, cp->redo);
            }
            catch (const std::exception &e)
            {
                cp->undo.clear();
                cp->redo.clear();
                cp->historyWarning = e.what();
            }
        }
        return cp;
    }
    void RevisionPersistence::save(const std::filesystem::path &path,
                                   const RevisionCheckpoint &cp,
                                   const std::function<void()> &beforeReplace,
                                   uint64_t maxHistoryBytes,
                                   const std::function<bool()> &cancel)
    {
        auto archive = storage::RevisionArchive::open(path);
        std::lock_guard lock(archive->operationMutex);
        archive->setCancelCheck(cancel);
        struct ClearCancel
        {
            storage::RevisionArchive &archive;
            ~ClearCancel()
            {
                archive.setCancelCheck({});
            }
        } clearCancel{*archive};
        auto j = cp.metadata;
        j["current"] = saveState(*archive, cp.current);
        j["saved"] = saveState(*archive, cp.saved);
        j["preservation"] = archive->saveSource(cp.preservation);
        j["historyWarning"] = cp.historyWarning;
        auto saveHistory = [&](const auto &entries)
        {
            auto array = Json::array();
            for (const auto &h : entries)
            {
                array.push_back({{"details", h.details},
                                 {"before", saveState(*archive, h.before)},
                                 {"after", saveState(*archive, h.after)},
                                 {"copied", archive->save(h.copied)}});
            }
            return array;
        };
        if (maxHistoryBytes == UINT64_MAX)
        {
            maxHistoryBytes = undo::maxRestartUndoStoreBytes();
        }
        std::vector<std::shared_ptr<const storage::AudioEditRevision>>
            historyRoots;
        for (const auto *entries : {&cp.undo, &cp.redo})
        {
            for (const auto &h : *entries)
            {
                historyRoots.push_back(h.before.audio);
                historyRoots.push_back(h.after.audio);
                historyRoots.push_back(h.copied);
            }
        }
        const auto historyBytes = archive->additionalHistoryBytes(
            {cp.current.audio, cp.saved.audio}, cp.preservation, historyRoots);
        if (maxHistoryBytes && historyBytes > maxHistoryBytes)
        {
            j["historyWarning"] =
                "History-only storage uses " + std::to_string(historyBytes) +
                " bytes, exceeding the " + std::to_string(maxHistoryBytes) +
                " byte restart limit";
            j["undo"] = Json::array();
            j["redo"] = Json::array();
        }
        else
        {
            j["undo"] = saveHistory(cp.undo);
            j["redo"] = saveHistory(cp.redo);
        }
        if (!j.at("historyWarning").get<std::string>().empty())
        {
            logging::warn("Restart history omitted: " +
                          j.at("historyWarning").get<std::string>());
        }
        archive->commit(std::move(j), beforeReplace);
        if (cp.metadata.value("clipboard", false))
        {
            archive->retainClipboardStores(cp.current.audio);
        }
    }
    void RevisionPersistence::load(const std::filesystem::path &path,
                                   DocumentSession &session,
                                   const std::function<bool()> &cancel)
    {
        auto archive = storage::RevisionArchive::open(path);
        std::lock_guard lock(archive->operationMutex);
        archive->setCancelCheck(cancel);
        struct Clear
        {
            storage::RevisionArchive &a;
            ~Clear()
            {
                a.setCancelCheck({});
            }
        } clear{*archive};
        const auto j = archive->readManifest();
        auto cp = std::make_shared<RevisionCheckpoint>();
        cp->metadata = j;
        cp->current = loadState(*archive, j.at("current"));
        cp->saved = loadState(*archive, j.at("saved"));
        cp->preservation = archive->loadSource(j.at("preservation"));
        cp->historyWarning = j.at("historyWarning");
        auto loadHistory = [&](const auto &json, auto &entries)
        {
            for (const auto &h : json)
            {
                entries.push_back({loadState(*archive, h.at("before")),
                                   loadState(*archive, h.at("after")),
                                   archive->load(h.at("copied")),
                                   h.at("details")});
            }
        };
        loadHistory(j.at("undo"), cp->undo);
        loadHistory(j.at("redo"), cp->redo);
        if (!cp->current.audio)
        {
            throw std::runtime_error("Missing recovered document root");
        }
        DocumentSession restored;
        const auto shape = cp->current.audio->shape();
        restored.document.setExternalAudioShape(shape.format, shape.sampleRate,
                                                shape.channels, shape.frames);
        restored.document.adoptPreservationSourceId(j.at("sourceId"));
        restored.document.replaceMarkers(cp->current.markers);
        restored.bindReadRevision(cp->current.audio);
        restored.markRevisionSaved(cp->saved.audio, cp->saved.markers);
        restored.preservationSource =
            concurrency::releaseOnWorker(cp->preservation);
        restored.currentFile = j.at("file");
        restored.currentFileExportSettings = settings(j.at("settings"));
        restored.currentFileRequiresSaveAs = j.at("requiresSaveAs");
        restored.preservationReferenceFile = j.at("reference");
        restored.preservationReferenceExportSettings =
            settings(j.at("referenceSettings"));
        restored.overwritePreservationBrokenByOperation =
            j.at("preservationBroken");
        restored.overwritePreservationBrokenReason = j.at("preservationReason");
        restored.selection = cp->current.selection;
        restored.cursor = cp->current.cursor;
        restored.syncSelectionAndCursorToDocumentLength();
        restored.autosaveSnapshotPath = path;
        restored.autosavedWaveformDataVersion =
            restored.document.getWaveformDataVersion();
        restored.autosavedMarkerDataVersion =
            restored.document.getMarkerDataVersion();
        restored.recoveredRevisionCheckpoint =
            concurrency::releaseOnWorker(std::move(cp));
        session = std::move(restored);
    }
    bool RevisionPersistence::installHistory(State *state, int index)
    {
        auto &tab = state->tabs.at(index);
        auto cp = tab.session.recoveredRevisionCheckpoint;
        if (!cp)
        {
            return false;
        }
        auto restore = [&](const auto &entries)
        {
            std::deque<std::shared_ptr<actions::Undoable>> out;
            for (const auto &h : entries)
            {
                const Json &d = h.details;
                if (d.at("kind") == "revision")
                {
                    std::optional<ClipboardAudio> clip;
                    if (h.copied)
                    {
                        clip.emplace();
                        clip->assignRevision(h.copied);
                    }
                    auto edit = std::make_shared<actions::audio::RevisionEdit>(
                        state, index, d.at("name"), h.before, h.after,
                        std::move(clip), d.at("trim"));
                    edit->beforeView = view(d.at("beforeView"));
                    edit->afterView = view(d.at("afterView"));
                    edit->haveAfterView = d.at("haveAfterView");
                    out.push_back(std::move(edit));
                }
                else if (d.at("kind") == "point")
                {
                    auto point =
                        std::make_shared<actions::audio::SetSampleValue>(
                            state, d.at("channel"), d.at("sample"),
                            std::bit_cast<float>(d.at("old").get<uint32_t>()),
                            std::bit_cast<float>(d.at("new").get<uint32_t>()));
                    point->revisionBefore = h.before;
                    point->revisionAfter = h.after.audio;
                    point->appliedRevision = d.at("appliedAfter").get<bool>()
                                                 ? h.after.audio
                                                 : h.before.audio;
                    point->changedValue = false;
                    point->tabId = tab.id;
                    out.push_back(std::move(point));
                }
                else
                {
                    auto entry =
                        undo::restoreUndoEntry(state, index, d.at("entry"));
                    if (!entry)
                    {
                        throw std::runtime_error(
                            "Cannot restore history entry");
                    }
                    out.push_back(std::move(entry));
                }
            }
            return out;
        };
        try
        {
            auto undo = restore(cp->undo), redo = restore(cp->redo);
            tab.undoables = std::move(undo);
            tab.redoables = std::move(redo);
            tab.session.autosavedHistoryVersion = ++tab.historyVersion;
            if (cp->metadata.contains("view"))
            {
                tab.viewState = view(cp->metadata.at("view"));
            }
            if (!cp->historyWarning.empty())
            {
                state->startupRestore.historyRestoreFailed = true;
                logging::warn("Restart history omitted: " + cp->historyWarning);
            }
            tab.session.recoveredRevisionCheckpoint.reset();
            return true;
        }
        catch (const std::exception &e)
        {
            logging::warn(e.what());
            return false;
        }
    }
} // namespace cupuacu::persistence
