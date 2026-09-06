#include "RevisionArchive.hpp"
#include "../file/FileIo.hpp"
#include "../LongTask.hpp"
#include "../concurrency/DeferredRelease.hpp"
#include "../waveform/DecodedWaveformBuilder.hpp"
#include <bit>
#include <set>
#include <unordered_set>
#ifdef __APPLE__
#include <sys/clonefile.h>
#endif
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace cupuacu::storage
{
    namespace
    {
        std::mutex registryMutex;
        std::map<std::filesystem::path, std::shared_ptr<RevisionArchive>>
            registry;
        void sync(const std::filesystem::path &path, bool directory = false)
        {
#ifdef _WIN32
            if (directory)
            {
                return; // ReplaceFile/MoveFileEx provides replacement
                        // semantics.
            }
            auto handle = CreateFileW(
                path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (handle == INVALID_HANDLE_VALUE)
            {
                throw std::runtime_error("Cannot sync revision file");
            }
            const bool ok = FlushFileBuffers(handle);
            CloseHandle(handle);
#else
            const int fd = ::open(path.c_str(), O_RDONLY);
            if (fd < 0)
            {
                throw std::runtime_error("Cannot sync revision file");
            }
            const bool ok = ::fsync(fd) == 0;
            ::close(fd);
#endif
            if (!ok)
            {
                throw std::runtime_error("Revision file sync failed");
            }
        }
        uint32_t checksum(std::span<const uint8_t> bytes)
        {
            uint32_t h = 2166136261u;
            for (auto b : bytes)
            {
                h = (h ^ b) * 16777619u;
            }
            return h;
        }
        void put(std::ostream &out, uint32_t value)
        {
            for (int i = 0; i < 4; ++i)
            {
                out.put(char(value >> (i * 8)));
            }
        }
        uint32_t get(std::istream &in)
        {
            uint32_t v = 0;
            for (int i = 0; i < 4; ++i)
            {
                int b = in.get();
                if (b < 0)
                {
                    throw std::runtime_error("Truncated revision record");
                }
                v |= uint32_t(b) << (i * 8);
            }
            return v;
        }
        nlohmann::json shapeJson(AudioShape s)
        {
            return {s.frames, s.channels, s.sampleRate, int(s.format)};
        }
        AudioShape shapeFrom(const nlohmann::json &j)
        {
            AudioShape s{j.at(0).get<int64_t>(), j.at(1).get<int>(),
                         j.at(2).get<int>(), SampleFormat(j.at(3).get<int>())};
            if (s.frames < 0 || s.channels <= 0 || s.channels > 256 ||
                s.sampleRate <= 0 || s.frames > INT64_MAX / s.channels / 4 ||
                s.format < SampleFormat::PCM_S8 ||
                s.format >= SampleFormat::Unknown)
            {
                throw std::runtime_error("Invalid revision shape");
            }
            return s;
        }
        std::string localName(const nlohmann::json &j)
        {
            auto s = j.get<std::string>();
            if (s.empty() || s == "." || s == ".." ||
                s.find_first_of("/\\:") != std::string::npos)
            {
                throw std::runtime_error("Invalid archive file name");
            }
            return s;
        }
    } // namespace
    RevisionArchive::RevisionArchive(std::filesystem::path path)
        : manifestPath(std::move(path))
    {
        std::string generation;
        if (recognizes(manifestPath))
        {
            std::ifstream in(manifestPath);
            Json j;
            in >> j;
            generation = localName(j.at("generation"));
        }
        else
        {
            generation = "g" + std::to_string(std::chrono::steady_clock::now()
                                                  .time_since_epoch()
                                                  .count());
        }
        directory =
            std::filesystem::path(manifestPath.string() + ".revisions") /
            generation;
    }
    RevisionArchive::~RevisionArchive()
    {
        if (removed)
        {
            std::error_code ec;
            std::filesystem::remove_all(directory, ec);
            std::filesystem::remove(directory.parent_path(), ec);
        }
    }
    std::shared_ptr<RevisionArchive>
    RevisionArchive::open(const std::filesystem::path &path)
    {
        const auto key = std::filesystem::absolute(path).lexically_normal();
        std::lock_guard lock(registryMutex);
        if (auto found = registry.find(key); found != registry.end())
        {
            return found->second;
        }
        auto archive =
            std::shared_ptr<RevisionArchive>(new RevisionArchive(key));
        registry.emplace(key, archive);
        return archive;
    }
    bool RevisionArchive::hasLiveReaders(const std::filesystem::path &path)
    {
        const auto key = std::filesystem::absolute(path).lexically_normal();
        std::lock_guard lock(registryMutex);
        auto found = registry.find(key);
        if (found == registry.end()) return false;
        std::lock_guard operation(found->second->operationMutex);
        for (const auto &[name, store] : found->second->loadedStores)
            if (!store.expired()) return true;
        return false;
    }
    void RevisionArchive::remove(const std::filesystem::path &path)
    {
        const auto key = std::filesystem::absolute(path).lexically_normal();
        std::shared_ptr<RevisionArchive> retired;
        {
            std::lock_guard registryLock(registryMutex);
            auto found = registry.find(key);
            std::shared_ptr<RevisionArchive> archive;
            try
            {
                archive = found == registry.end()
                              ? std::shared_ptr<RevisionArchive>(
                                    new RevisionArchive(key))
                              : found->second;
            }
            catch (const nlohmann::json::exception &)
            {
                // A malformed manifest cannot identify its generation safely.
                // Remove the manifest, retaining unidentified data for
                // recovery.
                std::error_code ec;
                std::filesystem::remove(key, ec);
                return;
            }
            std::lock_guard publicationLock(archive->publicationMutex);
            archive->removed.store(true, std::memory_order_release);
            std::error_code ec;
            std::filesystem::remove(key, ec);
            if (found != registry.end())
            {
                registry.erase(found);
            }
            retired = concurrency::releaseOnWorker(std::move(archive));
        }
        // No operation lock or join: long copies notice cancellation; live
        // loaded stores keep their generation until their last reader exits.
    }
    bool RevisionArchive::recognizes(const std::filesystem::path &path)
    {
        std::ifstream in(path, std::ios::binary);
        return in.peek() == '{';
    }
    void RevisionArchive::check() const
    {
        if (removed)
        {
            throw std::runtime_error("Revision archive was removed");
        }
        if (canceled && canceled())
        {
            throw LongTaskCanceledError{};
        }
    }
    uint64_t RevisionArchive::append(const Json &json)
    {
        check();
        std::filesystem::create_directories(directory);
        const auto path = directory / "index.bin";
        if (!std::filesystem::exists(path))
        {
            std::ofstream out(path, std::ios::binary);
            out.write("CRV1", 4);
        }
        const auto bytes = Json::to_cbor(json);
        if (bytes.size() > 128 * 1024 * 1024)
        {
            throw std::length_error("Revision record too large");
        }
        const auto id = std::filesystem::file_size(path);
        std::ofstream out(path, std::ios::binary | std::ios::app);
        put(out, uint32_t(bytes.size()));
        put(out, checksum(bytes));
        out.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
        out.close();
        if (!out)
        {
            throw std::runtime_error("Revision index write failed");
        }
        stats.metadataBytes += bytes.size() + 8;
        return id;
    }
    RevisionArchive::Json RevisionArchive::record(uint64_t id)
    {
        check();
        if (id < 4 || id > readLimit || readLimit - id < 8)
        {
            throw std::runtime_error(
                "Revision reference outside committed index");
        }
        std::ifstream in(directory / "index.bin", std::ios::binary);
        in.seekg(id);
        const auto size = get(in), hash = get(in);
        if (size > 128 * 1024 * 1024 || size > readLimit - id - 8)
        {
            throw std::runtime_error("Invalid revision record length");
        }
        std::vector<uint8_t> bytes(size);
        in.read(reinterpret_cast<char *>(bytes.data()), size);
        if (!in || checksum(bytes) != hash)
        {
            throw std::runtime_error("Corrupt revision record");
        }
        return Json::from_cbor(bytes);
    }
    RevisionArchive::StoreCopy &
    RevisionArchive::copyStore(const AudioRevision &source)
    {
        auto &copy = stores[source.store->id()];
        if (copy.name.empty())
        {
            copy.name = "store-" + std::to_string(append({{"kind", "store"}}));
        }
        const auto destination = directory / copy.name;
        std::filesystem::create_directories(destination);
        // Snapshot the committed prefix under the lock. Appends never change
        // that prefix; copying it must not block playback cache misses.
        std::vector<uint64_t> lengths;
        {
            std::lock_guard lock(source.store->mutex);
            if (source.store->writer.is_open() && !source.store->failed)
                source.store->writer.flush();
            lengths = source.store->lengths;
        }
        auto copyFile =
            [&](const auto &from, const auto &to, uint64_t begin, uint64_t end)
        {
            if (begin == end && std::filesystem::exists(to))
            {
                return;
            }
#ifdef __APPLE__
            if (begin == 0 && !std::filesystem::exists(to) &&
                std::filesystem::file_size(from) == end &&
                clonefile(from.c_str(), to.c_str(), 0) == 0)
            {
                sync(to);
                return;
            }
#endif
            std::ifstream in(from, std::ios::binary);
            in.seekg(begin);
            // A failed prior attempt may have left an uncommitted tail.
            if (std::filesystem::exists(to))
                std::filesystem::resize_file(to, begin);
            std::ofstream out(to, std::ios::binary | std::ios::app);
            std::array<char, 65536> buffer;
            while (begin < end)
            {
                check();
                const auto count =
                    std::min<uint64_t>(buffer.size(), end - begin);
                in.read(buffer.data(), count);
                out.write(buffer.data(), count);
                if (!in || !out)
                    throw std::runtime_error("Revision audio copy failed");
                begin += count;
            }
            out.close();
            if (!out)
                throw std::runtime_error("Revision audio close failed");
            sync(to);
        };
        for (std::size_t i = 0; i < lengths.size(); ++i)
        {
            if (copy.lengths.size() <= i)
            {
                copy.lengths.push_back(0);
            }
            const auto length = lengths[i];
            if (length > copy.lengths[i])
            {
                copyFile(source.store->segmentPath(i),
                         destination /
                             ("samples-" + std::to_string(i) + ".bin"),
                         copy.lengths[i], length);
                stats.sampleBytes += length - copy.lengths[i];
                copy.lengths[i] = length;
            }
        }
        if (!source.ownedSource.empty() && copy.source.empty())
        {
            const auto name =
                "source" + source.ownedSource.extension().string();
            const auto size = std::filesystem::file_size(source.ownedSource);
            copyFile(source.ownedSource, destination / name, 0, size);
            stats.sourceBytes += size;
            copy.source = name;
        }
        sync(destination, true);
        return copy;
    }
    uint64_t RevisionArchive::saveSource(
        const std::shared_ptr<const AudioRevision> &source)
    {
        if (!source)
        {
            return 0;
        }
        if (auto it = sources.find(source->identity); it != sources.end())
        {
            return it->second;
        }
        const auto &copy = copyStore(*source);
        Json blocks = Json::array(), metadata = Json::array(),
             peaks = Json::array();
        for (const auto &channel : source->channels)
        {
            auto row = Json::array();
            for (auto b : channel)
            {
                row.push_back({b.segment, b.offset, b.frames});
            }
            blocks.push_back(std::move(row));
        }
        for (const auto &channel : source->metadata)
        {
            auto row = Json::array();
            for (auto r : channel)
            {
                row.push_back({r.start, r.frames, r.provenance.sourceId,
                               r.provenance.frameIndex, r.dirty});
            }
            metadata.push_back(std::move(row));
        }
        if (source->peaks)
        {
            for (int c = 0; c < source->shape().channels; ++c)
            {
                const auto &channel = source->peaks->channels[c];
                auto levels = Json::array();
                for (std::size_t l = 0; l < channel.size(); ++l)
                {
                    const auto count = source->peaks->levelSize(c, l);
                    auto pages = Json::array();
                    for (std::size_t first = 0; first < count; first += 8192)
                    {
                        std::vector<uint8_t> bytes;
                        const auto end = std::min(count, first + 8192);
                        std::vector<waveform::Peak> values(end - first);
                        source->peaks->readPeaks(c, l, first, values);
                        bytes.reserve((end - first) * 8);
                        for (auto i = first; i < end; ++i)
                        {
                            for (auto bits : {std::bit_cast<uint32_t>(
                                                  values[i - first].min),
                                              std::bit_cast<uint32_t>(
                                                  values[i - first].max)})
                            {
                                for (int shift = 0; shift < 32; shift += 8)
                                {
                                    bytes.push_back(uint8_t(bits >> shift));
                                }
                            }
                        }
                        pages.push_back(append(
                            {{"kind", "peaks"},
                             {"bytes", Json::binary(std::move(bytes))}}));
                    }
                    levels.push_back(
                        {{"count", count}, {"pages", std::move(pages)}});
                }
                peaks.push_back(std::move(levels));
            }
        }
        const auto id = append({{"kind", "source"},
                                {"shape", shapeJson(source->shape())},
                                {"store", copy.name},
                                {"lengths", copy.lengths},
                                {"original", copy.source},
                                {"sourceId", source->preservationSourceId},
                                {"blocks", std::move(blocks)},
                                {"metadata", std::move(metadata)},
                                {"peaks", std::move(peaks)}});
        sources[source->identity] = id;
        return id;
    }
    uint64_t RevisionArchive::saveNode(const Tree &tree)
    {
        if (!tree)
        {
            return 0;
        }
        if (auto it = nodes.find(tree->identity); it != nodes.end())
        {
            return it->second;
        }
        Json j;
        if (tree->height == 1)
        {
            const auto &r = tree->range;
            j = {{"kind", "leaf"},
                 {"source", saveSource(r.source)},
                 {"channel", r.channel},
                 {"start", r.start},
                 {"frames", r.frames},
                 {"value", std::bit_cast<uint32_t>(r.constantValue)}};
        }
        else
        {
            j = {{"kind", "branch"},
                 {"left", saveNode(tree->left)},
                 {"right", saveNode(tree->right)}};
        }
        const auto id = append(j);
        nodes[tree->identity] = id;
        ++stats.nodes;
        return id;
    }
    uint64_t
    RevisionArchive::save(const std::shared_ptr<const AudioEditRevision> &audio)
    {
        if (!audio)
        {
            return 0;
        }
        if (auto it = roots.find(audio->identity); it != roots.end())
        {
            return it->second;
        }
        auto channels = Json::array();
        for (auto &root : audio->channels)
        {
            channels.push_back(saveNode(root));
        }
        auto id = append({{"kind", "revision"},
                          {"shape", shapeJson(audio->shape())},
                          {"channels", channels}});
        roots[audio->identity] = id;
        return id;
    }
    std::shared_ptr<const AudioRevision>
    RevisionArchive::loadSource(uint64_t id)
    {
        if (!id)
        {
            return {};
        }
        if (auto value = loadedSources[id].lock())
        {
            return value;
        }
        auto j = record(id);
        if (j.at("kind") != "source")
        {
            throw std::runtime_error("Expected source record");
        }
        auto shape = shapeFrom(j.at("shape"));
        const auto name = localName(j.at("store"));
        auto store = loadedStores[name].lock();
        const auto lengths = j.at("lengths").get<std::vector<uint64_t>>();
        if (!store)
        {
            store.reset(new AudioBlockStore);
            store->directory = directory / name;
            store->removeOnDestroy = false;
            store->failed = true;
            // A clipboard may outlive the manifest that first referenced it.
            // Revisit obsolete stores after its last reader releases the lease.
            auto lease =
                std::shared_ptr<int>(new int(0),
                                     [owner = shared_from_this()](int *p)
                                     {
                                         delete p;
                                         owner->collectUnusedStores();
                                     });
            store->archiveOwner =
                concurrency::releaseOnWorker(std::move(lease));
            loadedStores[name] = store;
        }
        {
            std::lock_guard storeLock(store->mutex);
            for (std::size_t i = 0; i < lengths.size(); ++i)
            {
                if (lengths[i] > uint64_t(INT64_MAX) ||
                    std::filesystem::file_size(store->segmentPath(i)) <
                        lengths[i])
                {
                    throw std::runtime_error(
                        "Truncated revision audio segment");
                }
                if (store->lengths.size() <= i)
                {
                    store->lengths.push_back(lengths[i]);
                }
                else
                {
                    store->lengths[i] = std::max(store->lengths[i], lengths[i]);
                }
            }
        }
        auto audio = std::shared_ptr<AudioRevision>(
            new AudioRevision(shape, store, cache));
        if (j.at("blocks").size() != std::size_t(shape.channels))
        {
            throw std::runtime_error("Invalid source channels");
        }
        for (int c = 0; c < shape.channels; ++c)
        {
            int64_t total = 0;
            for (const auto &b : j.at("blocks").at(c))
            {
                AudioBlock block{b.at(0).get<uint64_t>(),
                                 b.at(1).get<uint64_t>(),
                                 b.at(2).get<uint32_t>()};
                if (!block.frames || block.frames > AudioBlockFrames ||
                    block.frames > shape.frames - total ||
                    (block.frames != AudioBlockFrames &&
                     total + block.frames != shape.frames) ||
                    block.segment >= lengths.size() ||
                    block.offset > lengths[block.segment] ||
                    block.frames * 4ull > lengths[block.segment] - block.offset)
                {
                    throw std::runtime_error("Invalid persisted audio block");
                }
                total += block.frames;
                audio->channels[c].push_back(block);
            }
            if (total != shape.frames)
            {
                throw std::runtime_error("Incomplete source blocks");
            }
        }
        const auto original = j.at("original").get<std::string>();
        if (!original.empty())
        {
            audio->ownedSource = store->directory / localName(j.at("original"));
            if (!std::filesystem::is_regular_file(audio->ownedSource))
            {
                throw std::runtime_error("Missing original audio bytes");
            }
        }
        audio->preservationSourceId = j.at("sourceId");
        stores[store->id()] = {name, store->lengths, original};
        if (!j.at("metadata").empty())
        {
            if (j.at("metadata").size() != std::size_t(shape.channels))
            {
                throw std::runtime_error("Invalid metadata channels");
            }
            for (const auto &channel : j.at("metadata"))
            {
                auto &runs = audio->metadata.emplace_back();
                int64_t total = 0;
                for (const auto &r : channel)
                {
                    AudioRevision::MetadataRun run{
                        r.at(0), r.at(1), {r.at(2), r.at(3)}, r.at(4)};
                    if (run.start != total || run.frames <= 0 ||
                        run.frames > shape.frames - total ||
                        (run.provenance.isValid() &&
                         (run.provenance.frameIndex < 0 ||
                          run.frames > INT64_MAX - run.provenance.frameIndex)))
                    {
                        throw std::runtime_error("Invalid provenance run");
                    }
                    total += run.frames;
                    runs.push_back(run);
                }
                if (total != shape.frames)
                {
                    throw std::runtime_error("Incomplete provenance");
                }
            }
        }
        if (!j.at("peaks").empty())
        {
            std::vector<std::vector<gui::PeakLevel>> channels;
            try
            {
                for (const auto &channel : j.at("peaks"))
                {
                    auto &levels = channels.emplace_back();
                    for (const auto &level : channel)
                    {
                        auto &out = levels.emplace_back();
                        const auto count = level.at("count").get<uint64_t>();
                        if (count > uint64_t(shape.frames / 128 + 1))
                        {
                            throw std::runtime_error(
                                "Invalid source peak count");
                        }
                        out.resize(count);
                        std::size_t first = 0;
                        for (const auto &ref : level.at("pages"))
                        {
                            if (ref.get<uint64_t>() >= id)
                            {
                                throw std::runtime_error(
                                    "Invalid source peak reference");
                            }
                            const auto page = record(ref);
                            if (page.at("kind") != "peaks")
                            {
                                throw std::runtime_error("Invalid peak page");
                            }
                            const auto &bytes = page.at("bytes").get_binary();
                            if (bytes.size() % 8 || bytes.size() > 65536 ||
                                bytes.size() / 8 > out.size() - first)
                            {
                                throw std::runtime_error(
                                    "Invalid source peak bytes");
                            }
                            auto number = [&](std::size_t at)
                            {
                                uint32_t v = 0;
                                for (int n = 0; n < 4; ++n)
                                {
                                    v |= uint32_t(bytes[at + n]) << (n * 8);
                                }
                                return std::bit_cast<float>(v);
                            };
                            for (std::size_t i = 0; i < bytes.size() / 8; ++i)
                            {
                                out.set(first++,
                                        {number(i * 8), number(i * 8 + 4)});
                            }
                        }
                        if (first != out.size())
                        {
                            throw std::runtime_error("Incomplete peak level");
                        }
                    }
                }
                audio->peaks = waveform::SourcePeaks::createPaged(
                    shape, std::move(channels), cache, canceled);
            }
            catch (const LongTaskCanceledError &)
            {
                throw;
            }
            catch (const std::exception &)
            {
                // Summaries are rebuildable; a corrupt peak page cannot make
                // otherwise intact committed audio unrecoverable.
                waveform::DecodedWaveformBuilder builder;
                for (int64_t first = 0; first < shape.frames;)
                {
                    check();
                    first += std::min<int64_t>(65536, shape.frames - first);
                    builder.appendFrom(
                        shape, first,
                        [&](int c, int64_t at, std::span<float> out)
                        {
                            audio->readChannel(c, at, out);
                        });
                }
                auto caches = builder.takeCaches();
                channels.clear();
                for (int c = 0; c < shape.channels; ++c)
                {
                    channels.push_back(
                        caches.getCache(c).snapshotBuildState().levels);
                }
                audio->peaks = waveform::SourcePeaks::createPaged(
                    shape, std::move(channels), cache, canceled);
            }
        }
        loadedSources[id] = audio;
        sources[audio->identity] = id;
        return audio;
    }
    RevisionArchive::Tree RevisionArchive::loadNode(uint64_t id, int depth)
    {
        if (!id)
        {
            return {};
        }
        if (depth > 128)
        {
            throw std::runtime_error("Invalid revision tree depth");
        }
        if (auto value = loadedNodes[id].lock())
        {
            return value;
        }
        auto j = record(id);
        Tree tree;
        uint64_t allocated = 0;
        auto earlier = [&](uint64_t ref)
        {
            if (ref >= id)
            {
                throw std::runtime_error("Cyclic revision tree");
            }
            return ref;
        };
        if (j.at("kind") == "leaf")
        {
            AudioEditRevision::SourceRange r{
                loadSource(earlier(j.at("source"))), j.at("channel"),
                j.at("start"), j.at("frames"),
                std::bit_cast<float>(j.at("value").get<uint32_t>())};
            if (r.frames <= 0)
            {
                throw std::runtime_error("Invalid leaf duration");
            }
            if (r.source)
            {
                AudioReader::validateRange(r.source->shape(), r.channel,
                                           r.start, r.frames);
            }
            tree = AudioEditRevision::leaf(std::move(r), allocated);
        }
        else if (j.at("kind") == "branch")
        {
            auto left = loadNode(earlier(j.at("left")), depth + 1),
                 right = loadNode(earlier(j.at("right")), depth + 1);
            if (!left || !right || std::abs(left->height - right->height) > 1)
            {
                throw std::runtime_error("Invalid revision balance");
            }
            tree = AudioEditRevision::branch(std::move(left), std::move(right),
                                             allocated);
        }
        else
        {
            throw std::runtime_error("Invalid revision node kind");
        }
        loadedNodes[id] = tree;
        nodes[tree->identity] = id;
        return tree;
    }
    std::shared_ptr<const AudioEditRevision> RevisionArchive::load(uint64_t id)
    {
        if (!id)
        {
            return {};
        }
        if (auto value = loadedRoots[id].lock())
        {
            return value;
        }
        auto j = record(id);
        if (j.at("kind") != "revision")
        {
            throw std::runtime_error("Expected revision root");
        }
        auto shape = shapeFrom(j.at("shape"));
        std::vector<Tree> channels;
        for (const auto &ref : j.at("channels"))
        {
            if (ref.get<uint64_t>() >= id)
            {
                throw std::runtime_error("Invalid root reference");
            }
            auto tree = loadNode(ref);
            if (AudioEditRevision::length(tree) != shape.frames)
            {
                throw std::runtime_error("Invalid root duration");
            }
            channels.push_back(std::move(tree));
        }
        if (channels.size() != std::size_t(shape.channels))
        {
            throw std::runtime_error("Invalid root channels");
        }
        auto audio = std::shared_ptr<const AudioEditRevision>(
            new AudioEditRevision(shape, std::move(channels)));
        loadedRoots[id] = audio;
        roots[audio->identity] = id;
        return audio;
    }
    uint64_t RevisionArchive::additionalHistoryBytes(
        const std::vector<std::shared_ptr<const AudioEditRevision>> &base,
        const std::shared_ptr<const AudioRevision> &preservation,
        const std::vector<std::shared_ptr<const AudioEditRevision>> &history)
    {
        std::unordered_set<const AudioEditRevision::Node *> visited;
        std::unordered_set<const AudioBlockStore *> retained;
        uint64_t bytes = 0;
        auto source =
            [&](const std::shared_ptr<const AudioRevision> &s, bool count)
        {
            if (!s || !retained.insert(s->store.get()).second || !count)
            {
                return;
            }
            std::lock_guard lock(s->store->mutex);
            auto add = [&](uint64_t n)
            {
                if (n > UINT64_MAX - bytes)
                {
                    throw std::overflow_error("History byte count overflow");
                }
                bytes += n;
            };
            for (auto n : s->store->lengths)
            {
                add(n);
            }
            if (!s->ownedSource.empty())
            {
                add(std::filesystem::file_size(s->ownedSource));
            }
        };
        auto visit = [&](auto &&self, const Tree &node, bool count) -> void
        {
            if (!node || !visited.insert(node.get()).second)
            {
                return;
            }
            source(node->range.source, count);
            self(self, node->left, count);
            self(self, node->right, count);
        };
        auto roots = [&](const auto &audio, bool count)
        {
            for (const auto &a : audio)
            {
                if (a)
                {
                    for (const auto &root : a->channels)
                    {
                        visit(visit, root, count);
                    }
                }
            }
        };
        roots(base, false);
        source(preservation, false);
        roots(history, true);
        return bytes;
    }
    void RevisionArchive::retainClipboardStores(
        const std::shared_ptr<const AudioEditRevision> &audio)
    {
        neededStores.clear();
        pruneClipboardStores = true;
        if (audio)
        {
            for (int c = 0; c < audio->shape().channels; ++c)
            {
                audio->visitSourceRanges(
                    c, 0, audio->shape().frames,
                    [&](const auto &range)
                    {
                        if (range.source)
                        {
                            neededStores.insert(
                                stores.at(range.source->store->id()).name);
                        }
                    });
            }
        }
        collectUnusedStoresLocked();
    }
    void RevisionArchive::collectUnusedStores()
    {
        try
        {
            std::lock_guard lock(operationMutex);
            collectUnusedStoresLocked();
        }
        catch (...)
        {
        } // Cleanup can retry on the next checkpoint/release.
    }
    void RevisionArchive::collectUnusedStoresLocked()
    {
        if (!pruneClipboardStores || removed ||
            !std::filesystem::exists(directory))
        {
            return;
        }
        bool collected = false;
        for (const auto &entry : std::filesystem::directory_iterator(directory))
        {
            const auto name = entry.path().filename().string();
            if (!entry.is_directory() || !name.starts_with("store-") ||
                neededStores.contains(name))
            {
                continue;
            }
            if (auto live = loadedStores[name].lock())
            {
                continue;
            }
            std::error_code ec;
            std::filesystem::remove_all(entry.path(), ec);
            if (ec)
            {
                continue;
            }
            collected = true;
            std::erase_if(stores,
                          [&](const auto &item)
                          {
                              return item.second.name == name;
                          });
        }
        if (collected)
        {
            // Reusing an old runtime root must republish any reclaimed source
            // from its still-live working store, not reuse dangling log IDs.
            roots.clear();
            nodes.clear();
            sources.clear();
        }
    }
    void RevisionArchive::commit(Json manifest,
                                 const std::function<void()> &beforeReplace)
    {
        check();
        // Even an empty document needs a durable index header.
        if (!std::filesystem::exists(directory / "index.bin"))
        {
            append({{"kind", "empty"}});
        }
        sync(directory / "index.bin");
        sync(directory, true);
        sync(directory.parent_path(), true);
        manifest["magic"] = "CUPUACU_REVISION";
        manifest["version"] = 1;
        manifest["generation"] = directory.filename().string();
        manifest["logEnd"] =
            std::filesystem::file_size(directory / "index.bin");
        file::ensureParentDirectoryExists(manifestPath);
        const auto temporary = file::makeTemporarySiblingPath(manifestPath);
        file::detail::ScopedTemporaryFileCleanup cleanup(temporary);
        const auto bytes = manifest.dump();
        std::ofstream out(temporary, std::ios::binary);
        out << bytes;
        out.close();
        stats.metadataBytes += bytes.size();
        if (!out)
        {
            throw std::runtime_error("Revision manifest write failed");
        }
        sync(temporary);
        check();
        if (beforeReplace)
        {
            beforeReplace();
        }
        {
            // Closing a tab only waits for publication, never serialization
            // or flushing the temporary checkpoint.
            std::lock_guard publicationLock(publicationMutex);
            check();
            file::replaceFile(temporary, manifestPath);
            cleanup.dismiss();
        }
        sync(manifestPath.parent_path(), true);
    }
    RevisionArchive::Json RevisionArchive::readManifest()
    {
        check();
        if (std::filesystem::file_size(manifestPath) > 64 * 1024 * 1024)
        {
            throw std::runtime_error("Revision manifest too large");
        }
        std::ifstream in(manifestPath);
        Json j;
        in >> j;
        if (j.at("magic") != "CUPUACU_REVISION" || j.at("version") != 1)
        {
            throw std::runtime_error("Unsupported revision archive");
        }
        std::ifstream index(directory / "index.bin", std::ios::binary);
        std::array<char, 4> magic{};
        index.read(magic.data(), 4);
        if (!index || std::string_view(magic.data(), 4) != "CRV1")
        {
            throw std::runtime_error("Invalid revision index header");
        }
        readLimit = j.at("logEnd");
        if (readLimit > std::filesystem::file_size(directory / "index.bin"))
        {
            throw std::runtime_error("Truncated revision index");
        }
        return j;
    }
} // namespace cupuacu::storage
