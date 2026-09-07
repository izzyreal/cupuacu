#include "../LongTask.hpp"
#include "DecodedImportCache.hpp"
#include "../persistence/RevisionPersistence.hpp"
#include "../storage/AudioEditRevision.hpp"
#include "../Logger.hpp"
#include <iomanip>
#include <sstream>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace cupuacu::file
{
    namespace
    {
        std::atomic<unsigned> pending{0};
        uint64_t treeBytes(const std::filesystem::path &path)
        {
            uint64_t bytes = 0;
            if (std::filesystem::exists(path))
            {
                for (const auto &entry :
                     std::filesystem::recursive_directory_iterator(path))
                {
                    if (entry.is_regular_file() && !entry.is_symlink())
                    {
                        bytes += entry.file_size();
                    }
                }
            }
            return bytes;
        }
        std::shared_ptr<const void> lockCache(const std::filesystem::path &path)
        {
#ifdef _WIN32
            auto handle = CreateFileW(
                path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (handle == INVALID_HANDLE_VALUE)
            {
                return {};
            }
            return {handle, [](const void *p)
                    {
                        CloseHandle(const_cast<void *>(p));
                    }};
#else
            const int fd =
                ::open(path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
            if (fd < 0)
            {
                return {};
            }
            if (flock(fd, LOCK_EX | LOCK_NB) != 0)
            {
                ::close(fd);
                return {};
            }
            return {new int(fd), [](const void *p)
                    {
                        const auto *fd = static_cast<const int *>(p);
                        ::close(*fd);
                        delete fd;
                    }};
#endif
        }
        void touch(const std::filesystem::path &path)
        {
            std::error_code ec;
            std::filesystem::last_write_time(
                path, std::filesystem::file_time_type::clock::now(), ec);
        }
        std::unique_ptr<LoadedAudioFile>
        loaded(Document document, std::optional<AudioExportSettings> settings,
               bool requiresSaveAs,
               std::shared_ptr<const storage::AudioRevision> audio,
               const std::shared_ptr<storage::DecodedBlockCache> &samples,
               const std::shared_ptr<const void> &lease)
        {
            // Persisted provenance IDs are process-local, not globally unique.
            // Match fresh imports rather than aliasing another open document.
            document.markCurrentStateAsSavedSource();
            audio = audio->withSampleCache(samples, lease,
                                           document.getPreservationSourceId());
            auto result = std::make_unique<LoadedAudioFile>();
            result->document = std::move(document);
            result->exportSettings = std::move(settings);
            result->requiresSaveAs = requiresSaveAs;
            result->externalSamples = true;
            result->ownedSource = std::move(audio);
            result->audioRevision =
                storage::AudioEditRevision::from(result->ownedSource);
            result->waveformCachesReady = true;
            result->persistentWaveformCacheChecked = true;
            result->persistentWaveformCacheLoaded = true;
            result->decodedAudioCacheLoaded = true;
            return result;
        }
    } // namespace
    DecodedImportCache::DecodedImportCache(std::filesystem::path directory,
                                           uint64_t limit)
        : root(std::move(directory)), budget(limit)
    {
    }
    void DecodedImportCache::ensureLease()
    {
        if (initialized.load(std::memory_order_acquire))
        {
            return;
        }
        std::lock_guard lock(maintenanceMutex);
        if (initialized)
        {
            return;
        }
        if (!budget)
        {
            initialized.store(true, std::memory_order_release);
            return;
        }
        try
        {
            std::filesystem::create_directories(root);
            lease = lockCache(root / ".lock");
        }
        catch (const std::exception &)
        { /* Cache unavailable: ordinary import. */
        }
        initialized.store(true, std::memory_order_release);
    }
    std::string
    DecodedImportCache::sourceIdentity(const std::filesystem::path &path)
    {
        const auto normalized = std::filesystem::canonical(path);
        nlohmann::json key = {normalized.string(),
                              std::filesystem::file_size(normalized),
                              std::filesystem::last_write_time(normalized)
                                  .time_since_epoch()
                                  .count()};
#ifndef _WIN32
        struct stat info{};
        if (::stat(normalized.c_str(), &info))
        {
            throw std::runtime_error("Cannot inspect audio source");
        }
        key.push_back(info.st_dev);
        key.push_back(info.st_ino);
#ifdef __APPLE__
        key.push_back(info.st_ctimespec.tv_sec);
        key.push_back(info.st_ctimespec.tv_nsec);
#else
        key.push_back(info.st_ctim.tv_sec);
        key.push_back(info.st_ctim.tv_nsec);
#endif
#endif
        return key.dump();
    }
    std::filesystem::path
    DecodedImportCache::entryPath(const std::string &identity) const
    {
        uint64_t hash = 14695981039346656037ull;
        for (unsigned char c : identity)
        {
            hash = (hash ^ c) * 1099511628211ull;
        }
        std::ostringstream name;
        name << "v1-" << std::hex << std::setw(16) << std::setfill('0') << hash;
        return root / name.str();
    }
    uint64_t DecodedImportCache::diskBytes() const
    {
        return treeBytes(root);
    }
    bool DecodedImportCache::removeEntry(const std::filesystem::path &path)
    {
        const auto manifest = path / "manifest";
        if (storage::RevisionArchive::hasLiveReaders(manifest))
        {
            return false;
        }
        storage::RevisionArchive::remove(manifest);
        std::filesystem::remove_all(path);
        return true;
    }
    bool DecodedImportCache::makeRoom(uint64_t needed)
    {
        if (!lease || needed > budget)
        {
            return false;
        }
        uint64_t used = diskBytes();
        std::vector<std::filesystem::directory_entry> entries;
        for (const auto &entry : std::filesystem::directory_iterator(root))
        {
            if (entry.is_directory() && !entry.is_symlink() &&
                entry.path().filename().string().starts_with("v1-"))
            {
                entries.push_back(entry);
            }
        }
        auto stamp = [](const auto &entry)
        {
            const auto manifest = entry.path() / "manifest";
            return std::filesystem::last_write_time(
                std::filesystem::exists(manifest) ? manifest : entry.path());
        };
        std::sort(entries.begin(), entries.end(),
                  [&](const auto &a, const auto &b)
                  {
                      return stamp(a) < stamp(b);
                  });
        for (const auto &entry : entries)
        {
            const bool incomplete =
                !std::filesystem::exists(entry.path() / "manifest");
            if (used <= budget - needed && !incomplete)
            {
                continue;
            }
            const auto bytes = treeBytes(entry.path());
            if (removeEntry(entry.path()))
            {
                used -= bytes;
            }
        }
        return used <= budget - needed;
    }
    void DecodedImportCache::prune()
    {
        ensureLease();
        std::lock_guard lock(maintenanceMutex);
        (void)makeRoom(0);
    }
    std::unique_ptr<LoadedAudioFile> DecodedImportCache::load(
        const std::string &identity,
        const std::shared_ptr<storage::DecodedBlockCache> &samples,
        const LoadCancelCheck &cancel)
    {
        ensureLease();
        if (!budget || !lease)
        {
            return {};
        }
        detail::throwIfLoadCanceled(cancel);
        {
            std::lock_guard lock(liveMutex);
            auto found = live.find(identity);
            if (found != live.end())
            {
                if (auto audio = found->second.audio.lock())
                {
                    ++hits;
                    return loaded(found->second.metadata,
                                  found->second.settings,
                                  found->second.requiresSaveAs,
                                  std::move(audio), samples, lease);
                }
            }
        }
        std::lock_guard lock(maintenanceMutex);
        const auto path = entryPath(identity);
        const auto manifest = path / "manifest";
        try
        {
            (void)makeRoom(0);
            if (!std::filesystem::exists(manifest))
            {
                return {};
            }
            DocumentSession session;
            persistence::RevisionPersistence::load(manifest, session, cancel);
            if (session.recoveredRevisionCheckpoint->metadata.at(
                    "decodedImportIdentity") != identity)
            {
                return {}; // Hash collision: never substitute another source.
            }
            if (!session.preservationSource)
            {
                throw std::runtime_error("Missing cached source");
            }
            auto audio = session.preservationSource;
            auto result = loaded(
                std::move(session.document), session.currentFileExportSettings,
                session.currentFileRequiresSaveAs, audio, samples, lease);
            {
                std::lock_guard liveLock(liveMutex);
                live[identity] = {result->ownedSource, result->document,
                                  result->exportSettings,
                                  result->requiresSaveAs};
            }
            touch(manifest);
            ++hits;
            ++diskHits;
            return result;
        }
        catch (const LongTaskCanceledError &)
        {
            throw;
        }
        catch (const std::exception &)
        {
            // A cache is disposable; preserve any live readers and decode anew.
            try
            {
                (void)removeEntry(path);
            }
            catch (const std::exception &)
            {
            }
            return {};
        }
    }
    void DecodedImportCache::retain(
        const std::string &identity, const LoadedAudioFile &file,
        const std::shared_ptr<concurrency::TaskScheduler> &scheduler)
    {
        ensureLease();
        if (!budget || !lease || !file.ownedSource || !scheduler)
        {
            return;
        }
        {
            std::lock_guard lock(liveMutex);
            for (auto it = live.begin(); it != live.end();)
            {
                it =
                    it->second.audio.expired() ? live.erase(it) : std::next(it);
            }
            live[identity] = {file.ownedSource, file.document,
                              file.exportSettings, file.requiresSaveAs};
        }
        DocumentSession session;
        session.document = file.document;
        session.currentFile = nlohmann::json::parse(identity).at(0);
        session.currentFileExportSettings = file.exportSettings;
        session.currentFileRequiresSaveAs = file.requiresSaveAs;
        session.preservationReferenceFile = session.currentFile;
        session.preservationReferenceExportSettings = file.exportSettings;
        session.preservationSource = file.ownedSource;
        session.bindReadRevision(file.audioRevision);
        auto checkpoint = persistence::RevisionPersistence::capture(session);
        checkpoint->metadata["decodedImportIdentity"] = identity;
        const auto audio = file.ownedSource;
        const uint64_t decoded =
            uint64_t(audio->shape().frames) * audio->shape().channels * 4;
        if (decoded > budget)
        {
            return;
        }
        const auto sourceBytes =
            std::filesystem::file_size(audio->sourcePath());
        if (sourceBytes > budget - decoded)
        {
            return;
        }
        const uint64_t overhead = decoded / 16 + 1024 * 1024;
        if (overhead > budget - decoded - sourceBytes)
        {
            return;
        }
        const uint64_t estimated = decoded + sourceBytes + overhead;
        // Never occupy both bulk workers with cache writes or retain a queue
        // of full recordings. Live weak reuse still works when admission fails.
        if (filling.exchange(true))
        {
            return;
        }
        ++pending;
        try
        {
            (void)scheduler->submit(
                [self = shared_from_this(), checkpoint, identity, estimated]
                {
                    struct Done
                    {
                        DecodedImportCache &cache;
                        ~Done()
                        {
                            cache.filling.store(false);
                            --pending;
                        }
                    } done{*self};
                    std::lock_guard lock(self->maintenanceMutex);
                    const auto path = self->entryPath(identity);
                    try
                    {
                        if (sourceIdentity(checkpoint->metadata.at("file")
                                               .get<std::string>()) != identity)
                        {
                            return;
                        }
                        if (std::filesystem::exists(path / "manifest"))
                        {
                            return;
                        }
                        if (!self->makeRoom(estimated) ||
                            std::filesystem::space(self->root).available <
                                estimated + 1024ull * 1024 * 1024)
                        {
                            return;
                        }
                        persistence::RevisionPersistence::save(
                            path / "manifest", *checkpoint);
                        if (self->diskBytes() > self->budget)
                        {
                            (void)self->removeEntry(path);
                        }
                    }
                    catch (const std::exception &)
                    {
                        try
                        {
                            (void)self->removeEntry(path);
                        }
                        catch (const std::exception &)
                        {
                        }
                    }
                },
                {.priority =
                     concurrency::TaskScheduler::Priority::Maintenance});
        }
        catch (const std::exception &)
        {
            filling.store(false);
            --pending;
        }
    }
    bool DecodedImportCache::hasPendingWork()
    {
        return pending.load() != 0;
    }
} // namespace cupuacu::file
