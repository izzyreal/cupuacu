#include "LegacyRecovery.hpp"
#include "RevisionPersistence.hpp"
#include "../LongTask.hpp"
#include "../file/OwnedSourceFile.hpp"
#include "../waveform/DecodedWaveformBuilder.hpp"
#include "../actions/markers/EditCommands.hpp"
#include <bit>
#include <fstream>
#include <map>

namespace cupuacu::persistence
{
    std::shared_ptr<storage::AudioBlockStore>
    legacyRecoveryStore(const std::filesystem::path &parent)
    {
        static std::atomic<uint64_t> serial{0};
        const auto stamp =
            std::chrono::steady_clock::now().time_since_epoch().count();
        return std::make_shared<storage::AudioBlockStore>(
            parent / ("legacy-convert-" + std::to_string(stamp) + "-" +
                      std::to_string(++serial)));
    }
    namespace
    {
        struct NeedsResidentHistory
        {
        };
        using Json = nlohmann::json;
        using Revision = storage::AudioEditRevision;
        using Audio = std::shared_ptr<const Revision>;
        using EditState = actions::audio::RevisionEditState;
        using Transaction = storage::AudioEditTransaction;
        using Shape = storage::AudioShape;

        class Input
        {
            std::ifstream stream;
            uint64_t length;

        public:
            explicit Input(const std::filesystem::path &path)
                : stream(path, std::ios::binary),
                  length(std::filesystem::file_size(path))
            {
                if (!stream)
                {
                    throw std::runtime_error(
                        "Cannot open legacy history payload");
                }
            }
            uint64_t position()
            {
                const auto p = stream.tellg();
                if (p < 0)
                {
                    throw std::runtime_error("Invalid legacy history offset");
                }
                return uint64_t(p);
            }
            void seek(uint64_t p)
            {
                if (p > length)
                {
                    throw std::runtime_error(
                        "Truncated legacy history payload");
                }
                stream.seekg(std::streamoff(p));
                if (!stream)
                {
                    throw std::runtime_error(
                        "Cannot seek legacy history payload");
                }
            }
            void skip(uint64_t count)
            {
                const auto p = position();
                if (count > length - p)
                {
                    throw std::runtime_error(
                        "Truncated legacy history payload");
                }
                seek(p + count);
            }
            void read(void *out, std::size_t count)
            {
                stream.read(static_cast<char *>(out), std::streamsize(count));
                if (!stream)
                {
                    throw std::runtime_error(
                        "Truncated legacy history payload");
                }
            }
            uint64_t integer(int count = 8)
            {
                unsigned char bytes[8]{};
                read(bytes, count);
                uint64_t result = 0;
                for (int n = 0; n < count; ++n)
                {
                    result |= uint64_t(bytes[n]) << (8 * n);
                }
                return result;
            }
            int64_t count()
            {
                const auto n = integer();
                if (n > uint64_t(INT64_MAX))
                {
                    throw std::runtime_error("Invalid legacy history count");
                }
                return int64_t(n);
            }
            void magic(const std::string &expected)
            {
                std::string actual(expected.size() + 1, '\0');
                read(actual.data(), actual.size());
                if (actual.compare(0, expected.size(), expected) ||
                    actual.back() != '\0')
                {
                    throw std::runtime_error(
                        "Invalid legacy history payload type");
                }
            }
        };
        struct Channel
        {
            uint64_t offset;
            int64_t frames;
            uint32_t stride = 4;
        };

        class Converter
        {
            std::filesystem::path root;
            std::shared_ptr<storage::AudioBlockStore> store;
            std::shared_ptr<storage::DecodedBlockCache> cache =
                storage::defaultDecodedBlockCache();
            std::function<bool()> cancel;
            std::map<std::string, std::vector<Audio>> loaded;

            Audio import(Input &input, const std::vector<Channel> &channels,
                         Shape shape)
            {
                if (channels.empty() || channels.size() > 256)
                {
                    throw std::runtime_error("Invalid legacy history channels");
                }
                shape.channels = int(channels.size());
                shape.frames = channels[0].frames;
                for (const auto &c : channels)
                {
                    if (c.frames != shape.frames)
                    {
                        throw std::runtime_error(
                            "Unequal legacy history channels");
                    }
                }
                if (!store)
                {
                    store = legacyRecoveryStore(root);
                }
                waveform::DecodedWaveformBuilder peaks;
                storage::AudioRevisionBuilder builder(
                    shape, store, cache,
                    [&](int64_t first, auto blocks, uint32_t count)
                    {
                        peaks.appendFrom(
                            shape, first + count,
                            [&](int c, int64_t at, std::span<float> out)
                            {
                                std::copy_n(blocks[c].data() + at - first,
                                            out.size(), out.data());
                            });
                    });
                constexpr int64_t batch = 16384;
                std::vector<float> samples(batch * channels.size());
                std::array<unsigned char, batch * 20> bytes;
                for (int64_t first = 0; first < shape.frames;)
                {
                    check();
                    const auto count = std::min(batch, shape.frames - first);
                    for (std::size_t c = 0; c < channels.size(); ++c)
                    {
                        const auto &ch = channels[c];
                        input.seek(ch.offset + uint64_t(first) * ch.stride);
                        input.read(bytes.data(), count * ch.stride);
                        for (int64_t i = 0; i < count; ++i)
                        {
                            const auto *p = bytes.data() + i * ch.stride;
                            const uint32_t bits =
                                uint32_t(p[0]) | uint32_t(p[1]) << 8 |
                                uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
                            samples[i * channels.size() + c] =
                                std::bit_cast<float>(bits);
                        }
                    }
                    builder.appendInterleaved(
                        std::span(samples).first(count * channels.size()));
                    first += count;
                }
                auto caches = peaks.takeCaches();
                std::vector<std::vector<gui::PeakLevel>> levels;
                for (int c = 0; c < shape.channels && shape.frames; ++c)
                {
                    levels.push_back(
                        caches.getCache(c).snapshotBuildState().levels);
                }
                return Revision::from(builder.finish(
                    {}, shape.frames ? std::make_shared<waveform::SourcePeaks>(
                                           shape, std::move(levels))
                                     : nullptr));
            }
            std::vector<Channel> matrix(Input &in)
            {
                const auto count = in.count();
                if (count > 256)
                {
                    throw std::runtime_error("Invalid legacy matrix channels");
                }
                std::vector<Channel> out;
                for (int64_t c = 0; c < count; ++c)
                {
                    const auto frames = in.count();
                    if (frames > INT64_MAX / 4)
                    {
                        throw std::runtime_error("Legacy matrix too long");
                    }
                    out.push_back({in.position(), frames});
                    in.skip(uint64_t(frames) * 4);
                }
                return out;
            }
            std::vector<Audio> payload(const Json &entry, const char *key,
                                       Shape shape, bool segment = false,
                                       bool cube = false)
            {
                const auto path = entry.at(key).get<std::string>();
                const auto cacheKey = path + ":" +
                                      std::to_string(shape.sampleRate) + ":" +
                                      std::to_string(int(shape.format));
                if (auto it = loaded.find(cacheKey); it != loaded.end())
                {
                    return it->second;
                }
                check();
                Input in(path);
                std::vector<std::vector<Channel>> matrices;
                if (segment)
                {
                    in.magic("CUPUACU_UNDO_SEGMENT");
                    const auto version = in.integer(4);
                    if (version < 1 || version > 3)
                    {
                        throw std::runtime_error(
                            "Unknown legacy segment version");
                    }
                    (void)in.integer(4);
                    (void)in.integer(
                        4); // destination interprets existing samples
                    const auto channels = in.count(), frames = in.count();
                    if (channels <= 0 || channels > 256 ||
                        frames > INT64_MAX / 20)
                    {
                        throw std::runtime_error(
                            "Invalid legacy segment shape");
                    }
                    auto &out = matrices.emplace_back();
                    for (int c = 0; c < channels; ++c)
                    {
                        out.push_back(
                            {in.position(), frames, version == 1 ? 20u : 4u});
                        in.skip(uint64_t(frames) * out.back().stride);
                        if (version == 1)
                        {
                            continue;
                        }
                        if (version == 3)
                        {
                            in.skip(frames);
                        }
                        const auto runs = in.count();
                        if (runs > frames)
                        {
                            throw std::runtime_error(
                                "Invalid legacy provenance count");
                        }
                        for (int64_t r = 0; r < runs; ++r)
                        {
                            check();
                            const auto first = in.count(), end = in.count();
                            const auto id = in.integer();
                            const auto source = int64_t(in.integer());
                            if (first > end || end > frames ||
                                (id && (source < 0 ||
                                        end - first > INT64_MAX - source)))
                            {
                                throw std::runtime_error(
                                    "Invalid legacy provenance range");
                            }
                        }
                    }
                }
                else
                {
                    in.magic(cube ? "CUPUACU_UNDO_SAMPLE_CUBE"
                                  : "CUPUACU_UNDO_SAMPLE_MATRIX");
                    if (in.integer(4) != 1)
                    {
                        throw std::runtime_error(
                            "Unknown legacy matrix version");
                    }
                    const auto count = cube ? in.count() : 1;
                    for (int64_t i = 0; i < count; ++i)
                    {
                        check();
                        matrices.push_back(matrix(in));
                    }
                }
                std::vector<Audio> out;
                for (const auto &m : matrices)
                {
                    out.push_back(m.empty() ? Audio{} : import(in, m, shape));
                }
                loaded.emplace(cacheKey, out);
                return out;
            }
            static void selection(EditState &s, bool active, double first = 0,
                                  double end = 0)
            {
                s.selection.setHighest(s.audio->shape().frames);
                s.selection.reset();
                if (active)
                {
                    s.selection.setValue1(first);
                    s.selection.setValue2(end);
                }
            }
            static void shift(EditState &s, int64_t at, int64_t removed,
                              int64_t inserted)
            {
                for (auto &m : s.markers)
                {
                    if (removed && m.frame >= at)
                    {
                        m.frame =
                            m.frame >= at + removed ? m.frame - removed : at;
                    }
                    if (inserted && m.frame >= at)
                    {
                        m.frame += inserted;
                    }
                }
            }
            Audio one(const Json &j, const char *key, Shape shape,
                      bool segment = false)
            {
                return payload(j, key, shape, segment).at(0);
            }

        public:
            Converter(std::filesystem::path root, std::function<bool()> cancel)
                : root(std::move(root)), cancel(std::move(cancel))
            {
            }
            void check() const
            {
                if (cancel && cancel())
                {
                    throw LongTaskCanceledError{};
                }
            }
            EditState apply(EditState s, const Json &j, bool redo)
            {
                check();
                const std::string kind = j.at("kind");
                const auto shape = s.audio->shape();
                const int64_t start = j.value("startFrame", int64_t{0});
                if (start < 0)
                {
                    throw std::runtime_error("Invalid legacy history start");
                }
                Transaction tx(*s.audio);
                auto replace = [&](int64_t at, int64_t remove, Audio insert)
                {
                    if (insert)
                    {
                        if (insert->shape().channels != shape.channels)
                        {
                            auto target = shape;
                            target.frames = insert->shape().frames;
                            Transaction mapped(*Revision::silence(target));
                            for (int c = 0;
                                 c < std::min(shape.channels,
                                              insert->shape().channels);
                                 ++c)
                            {
                                mapped.replaceChannel(c, 0, target.frames,
                                                      insert.get(), c);
                            }
                            insert = mapped.finish();
                        }
                        tx.replace(at, remove, *insert);
                    }
                    else
                    {
                        tx.erase(at, remove);
                    }
                    shift(s, at, remove, insert ? insert->shape().frames : 0);
                };
                bool setOldSelection = false;
                if (kind == "cut" || kind == "delete")
                {
                    const auto count = j.at("frameCount").get<int64_t>();
                    replace(start, redo ? count : 0,
                            redo ? Audio{}
                                 : one(j, "removedHandle", shape, true));
                    s.audio = tx.finish();
                    if (redo)
                    {
                        selection(s, false);
                        s.cursor = start;
                    }
                    else
                    {
                        setOldSelection = true;
                    }
                }
                else if (kind == "paste")
                {
                    const auto removed = j.at(redo ? "overwrittenFrameCount"
                                                   : "insertedFrameCount")
                                             .get<int64_t>();
                    const auto inserted = j.at(redo ? "insertedFrameCount"
                                                    : "overwrittenFrameCount")
                                              .get<int64_t>();
                    auto source = inserted ? one(j,
                                                 redo ? "insertedHandle"
                                                      : "overwrittenHandle",
                                                 shape, true)
                                           : Audio{};
                    if (source && source->shape().frames != inserted)
                    {
                        throw std::runtime_error(
                            "Legacy paste length mismatch");
                    }
                    replace(start, removed, source);
                    s.audio = tx.finish();
                    if (redo)
                    {
                        selection(s, true, start, start + inserted);
                        s.cursor = start;
                    }
                    else
                    {
                        setOldSelection = true;
                    }
                }
                else if (kind == "trim")
                {
                    const int64_t before = j.at("beforeCount"),
                                  middle = j.at("middleCount"),
                                  after = j.at("afterCount");
                    if (redo)
                    {
                        tx.trim(before, middle);
                        for (auto &m : s.markers)
                        {
                            m.frame = std::clamp(m.frame - before, int64_t{0},
                                                 middle);
                        }
                    }
                    else
                    {
                        if (before)
                        {
                            replace(0, 0, one(j, "beforeHandle", shape, true));
                        }
                        if (after)
                        {
                            replace(before + middle, 0,
                                    one(j, "afterHandle", shape, true));
                        }
                    }
                    s.audio = tx.finish();
                    selection(s, true, redo ? 0 : before,
                              (redo ? 0 : before) + middle);
                    s.cursor = redo ? 0 : before;
                }
                else if (kind == "set-sample-value")
                {
                    tx.replaceChannel(
                        j.at("channel"), j.at("sampleIndex"), 1, nullptr, 0, 0,
                        j.at(redo ? "newValue" : "oldValue").get<float>());
                    s.audio = tx.finish();
                }
                else if (kind == "set-marker-state")
                {
                    const auto marker =
                        actions::markers::markerSnapshotFromJson(
                            j.at(redo ? "newState" : "oldState"));
                    if (!marker)
                    {
                        throw std::runtime_error(
                            "Invalid legacy marker history");
                    }
                    std::erase_if(s.markers,
                                  [&](const auto &m)
                                  {
                                      return m.id == marker->marker.id;
                                  });
                    if (marker->exists)
                    {
                        s.markers.push_back(marker->marker);
                    }
                }
                else if (kind == "copy")
                {
                    if (redo)
                    {
                        selection(s, true, start,
                                  start + j.at("numFrames").get<int64_t>());
                        s.cursor = start;
                    }
                    else
                    {
                        selection(s, j.value("hadOldSelection", false),
                                  j.value("oldSel1", 0.0),
                                  j.value("oldSel2", 0.0));
                        s.cursor = j.value("oldCursorPos", int64_t{0});
                    }
                }
                else if (kind == "make-silent" || kind == "reverse" ||
                         kind == "amplify-fade" || kind == "amplify-envelope" ||
                         kind == "dynamics" || kind == "remove-silence-compact")
                {
                    const int64_t count = j.at("frameCount");
                    Audio source;
                    if (kind != "make-silent" || !redo)
                    {
                        source = one(j,
                                     kind == "make-silent" ? "originalHandle"
                                     : redo                ? "newSamplesHandle"
                                                           : "oldSamplesHandle",
                                     shape, kind == "make-silent");
                    }
                    int c = 0;
                    for (const auto &channel : j.at("targetChannels"))
                    {
                        tx.replaceChannel(
                            channel.get<int>(), start, count, source.get(),
                            kind == "make-silent" ? channel.get<int>() : c++);
                    }
                    s.audio = tx.finish();
                    if (kind == "make-silent" && !redo)
                    {
                        setOldSelection = true;
                    }
                }
                else if (kind == "remove-silence-duration")
                {
                    const auto &runs = j.at("runs");
                    int64_t removed = 0;
                    for (const auto &run : runs)
                    {
                        const int64_t count = run.at("frameCount");
                        if (count < 0 || count > INT64_MAX - removed)
                        {
                            throw std::runtime_error(
                                "Invalid legacy silence duration");
                        }
                        removed += count;
                    }
                    if (redo)
                    {
                        for (auto it = runs.rbegin(); it != runs.rend(); ++it)
                        {
                            replace(it->at("startFrame"), it->at("frameCount"),
                                    {});
                        }
                    }
                    else
                    {
                        auto sources = payload(j, "removedSamplesHandle", shape,
                                               false, true);
                        if (sources.size() != runs.size())
                        {
                            throw std::runtime_error(
                                "Legacy silence run count mismatch");
                        }
                        for (std::size_t i = 0; i < runs.size(); ++i)
                        {
                            replace(runs[i].at("startFrame"), 0, sources[i]);
                        }
                    }
                    s.audio = tx.finish();
                    const int64_t at = j.at("relevantStart"),
                                  length = j.at("originalRelevantLength");
                    selection(s, j.at("hadSelection"), at,
                              at + length - (redo ? removed : 0));
                    s.cursor =
                        redo ? at : j.at("originalCursor").get<int64_t>();
                }
                else if (kind == "record-edit")
                {
                    const int64_t oldFrames = j.at("oldFrameCount"),
                                  end = j.at("endFrame");
                    // Preserve the legacy recording command when undo changes
                    // the channel count or returns to an unconfigured document.
                    if (j.at("oldChannelCount").get<int>() != shape.channels ||
                        j.at("targetChannelCount").get<int>() !=
                            shape.channels ||
                        j.value("oldSampleRate", shape.sampleRate) !=
                            shape.sampleRate ||
                        j.value("oldFormat", int(shape.format)) !=
                            int(shape.format))
                    {
                        throw NeedsResidentHistory{};
                    }
                    if (oldFrames < 0 || end < start)
                    {
                        throw std::runtime_error(
                            "Invalid legacy recording range");
                    }
                    if (redo && end > shape.frames)
                    {
                        auto silence = shape;
                        silence.frames = end - shape.frames;
                        replace(shape.frames, 0, Revision::silence(silence));
                    }
                    if (!redo && shape.frames > oldFrames)
                    {
                        replace(oldFrames, shape.frames - oldFrames, {});
                    }
                    auto source = one(j,
                                      redo ? "recordedSamplesHandle"
                                           : "overwrittenOldSamplesHandle",
                                      shape);
                    const auto count =
                        redo ? end - start
                             : std::max<int64_t>(0, std::min(end, oldFrames) -
                                                        start);
                    if (count && source)
                    {
                        for (int c = 0; c < std::min(shape.channels,
                                                     source->shape().channels);
                             ++c)
                        {
                            tx.replaceChannel(c, start, count, source.get(), c);
                        }
                    }
                    s.audio = tx.finish();
                    selection(
                        s, j.at(redo ? "hadNewSelection" : "hadOldSelection"),
                        j.at(redo ? "newSelectionStart" : "oldSelectionStart"),
                        j.at(redo ? "newSelectionEnd" : "oldSelectionEnd"));
                    s.cursor = j.at(redo ? "newCursor" : "oldCursor");
                }
                else
                {
                    throw std::runtime_error(
                        "Unsupported legacy history command: " + kind);
                }
                if (setOldSelection)
                {
                    selection(s,
                              j.value("hadOldSelection",
                                      kind == "cut" || kind == "delete"),
                              j.value("oldSelectionStart", 0.0),
                              j.value("oldSelectionEnd", 0.0));
                    s.cursor = j.value("oldCursorPos", int64_t{0});
                }
                s.cursor =
                    std::clamp(s.cursor, int64_t{0}, s.audio->shape().frames);
                s.selection.setHighest(s.audio->shape().frames);
                std::sort(s.markers.begin(), s.markers.end(),
                          [](const auto &a, const auto &b)
                          {
                              return a.frame < b.frame ||
                                     (a.frame == b.frame && a.id < b.id);
                          });
                return s;
            }
            RevisionCheckpoint::History entry(const Json &j, EditState from,
                                              bool redo)
            {
                RevisionCheckpoint::History h;
                if (redo)
                {
                    h.before = from;
                    h.after = apply(from, j, true);
                    h.before = apply(h.after, j, false);
                    h.before.audio = from.audio;
                }
                else
                {
                    h.after = from;
                    h.before = apply(from, j, false);
                    h.after = apply(h.before, j, true);
                    h.after.audio = from.audio;
                }
                const std::string kind = j.at("kind");
                if (kind == "set-marker-state")
                {
                    h.details = {{"kind", "legacy"}, {"entry", j}};
                }
                else
                {
                    auto view = [](const Json &v) -> Json
                    {
                        return Json::array({v.value("samplesPerPixel", 1.0),
                                            v.value("verticalZoom", 1.0),
                                            v.value("sampleOffset", int64_t{0}),
                                            int(SelectedChannels::BOTH)});
                    };
                    static const std::map<std::string, std::string> names{
                        {"cut", "Cut"},
                        {"delete", "Delete"},
                        {"trim", "Trim"},
                        {"copy", "Copy"},
                        {"make-silent", "Make silent"},
                        {"reverse", "Reverse"},
                        {"amplify-fade", "Amplify/Fade"},
                        {"amplify-envelope", "Amplify Envelope"},
                        {"dynamics", "Dynamics"},
                        {"remove-silence-compact", "Remove silence"},
                        {"remove-silence-duration", "Remove silence"},
                        {"record-edit", "Record"},
                        {"set-sample-value", "Change sample value"}};
                    const auto description =
                        kind == "paste" ? (j.value("endFrame", int64_t{-1}) >= 0
                                               ? "Paste overwrite"
                                               : "Paste insert")
                                        : names.at(kind);
                    h.details = {
                        {"kind", "revision"},
                        {"name", description},
                        {"trim", kind == "trim"},
                        {"beforeView",
                         view(j.value("preTrimView", Json::object()))},
                        {"afterView",
                         view(j.value("postTrimView", Json::object()))},
                        {"haveAfterView", j.value("hasPostTrimView", false)}};
                    if (kind == "copy" || kind == "cut")
                    {
                        Transaction slice(*h.before.audio);
                        slice.trim(
                            j.at("startFrame"),
                            j.at(kind == "copy" ? "numFrames" : "frameCount"));
                        h.copied = slice.finish();
                    }
                }
                return h;
            }
        };
    } // namespace

    bool migrateLegacyHistory(DocumentSession &session,
                              const PersistedOpenDocumentState *state,
                              const std::filesystem::path &root,
                              const std::function<bool()> &cancel)
    {
        if (state)
        {
            session.cursor = std::clamp(state->cursor.value_or(0), int64_t{0},
                                        session.document.getFrameCount());
            session.selection.setHighest(session.document.getFrameCount());
            if (state->selectionStart && state->selectionEndExclusive)
            {
                session.selection.setValue1(*state->selectionStart);
                session.selection.setValue2(*state->selectionEndExclusive);
            }
            std::vector<DocumentMarker> markers;
            for (const auto &m : state->markers)
            {
                markers.push_back({m.id, m.frame, m.label});
            }
            session.document.replaceMarkers(std::move(markers));
            session.markRevisionSaved(session.getEditRevision(),
                                      session.document.getMarkers());
        }
        // Legacy snapshots do not contain an original saved revision. A named
        // recovered snapshot must remain dirty until explicitly saved.
        if (!session.currentFile.empty())
        {
            session.markRevisionSaved({}, {});
        }
        auto cp = RevisionPersistence::capture(session);
        if (state)
        {
            cp->metadata["view"] = Json::array(
                {state->samplesPerPixel.value_or(1.0), 1.0,
                 state->sampleOffset.value_or(0), int(SelectedChannels::BOTH)});
        }
        if (state && !state->undoStorePath.empty())
        {
            try
            {
                std::ifstream in(std::filesystem::path(state->undoStorePath) /
                                 "manifest.json");
                Json manifest;
                in >> manifest;
                if (manifest.at("version") != 1 ||
                    !manifest.at("entries").is_array())
                {
                    throw std::runtime_error("Invalid legacy history manifest");
                }
                Converter converter(root, cancel);
                auto build = [&](const Json &entries, auto &out, bool redo)
                {
                    if (!entries.is_array())
                    {
                        throw std::runtime_error(
                            "Invalid legacy history entries");
                    }
                    out.resize(entries.size());
                    auto current = cp->current;
                    // Both deques keep the next command at the back.
                    for (std::size_t i = entries.size(); i-- > 0;)
                    {
                        out[i] = converter.entry(entries[i], current, redo);
                        current = redo ? out[i].after : out[i].before;
                    }
                };
                build(manifest.at("entries"), cp->undo, false);
                build(manifest.value("redoEntries", Json::array()), cp->redo,
                      true);
            }
            catch (const NeedsResidentHistory &)
            {
                return false;
            }
            catch (const LongTaskCanceledError &)
            {
                throw;
            }
            catch (const std::exception &e)
            {
                // Keep a recoverable copy before a subsequent autosave can
                // replace the legacy snapshot or startup pruning can remove its
                // undo store.
                const auto retained =
                    root / "legacy-recovery-retained" /
                    ("recovery-" +
                     std::to_string(std::chrono::steady_clock::now()
                                        .time_since_epoch()
                                        .count()));
                std::filesystem::create_directories(retained);
                auto copy = [&](const std::filesystem::path &from,
                                const std::filesystem::path &to)
                {
                    std::filesystem::create_directories(to.parent_path());
                    file::cloneOrCopySource(
                        from, to,
                        [&](double)
                        {
                            if (cancel && cancel())
                            {
                                throw LongTaskCanceledError{};
                            }
                        });
                };
                copy(session.autosaveSnapshotPath,
                     retained / "snapshot.cupuacu-autosave");
                if (std::filesystem::is_directory(state->undoStorePath))
                {
                    for (const auto &file :
                         std::filesystem::recursive_directory_iterator(
                             state->undoStorePath))
                    {
                        if (file.is_regular_file())
                        {
                            copy(file.path(),
                                 retained / "undo" /
                                     std::filesystem::relative(
                                         file.path(), state->undoStorePath));
                        }
                    }
                }
                std::ofstream(retained / "original-paths.txt")
                    << session.autosaveSnapshotPath.string() << '\n'
                    << state->undoStorePath << '\n';
                cp->undo.clear();
                cp->redo.clear();
                cp->historyWarning = std::string(e.what()) +
                                     "; original recovery retained at " +
                                     retained.string();
            }
        }
        session.recoveredRevisionCheckpoint =
            concurrency::releaseOnWorker(std::move(cp));
        return true;
    }
} // namespace cupuacu::persistence
