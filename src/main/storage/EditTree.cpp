#include "EditTree.hpp"

namespace cupuacu::storage
{
    struct EditTree::Store : std::enable_shared_from_this<Store>
    {
        struct Record
        {
            uint64_t refs = 0, identity = 0, left = 0, right = 0, source = 0,
                     representative = 0;
            int64_t start = 0, frames = 0, leftFrames = 0, rightFrames = 0;
            uint64_t leftIdentity = 0, rightIdentity = 0;
            int leftHeight = 0, rightHeight = 0;
            int channel = 0, height = 0;
            float constantValue = 0;
            EditPeaks peaks;
            bool prepared = false;
        };
        struct Source
        {
            std::shared_ptr<void> memory;
            std::shared_ptr<const AudioRevision> value;
            uint64_t leaves = 0;
        };
        struct CachedRecord
        {
            uint64_t id = 0;
            Record value;
        };
        using HotRecords = std::array<CachedRecord, 512>;
        std::mutex mutex;
        std::shared_ptr<void> cacheMemory;
        std::unique_ptr<HotRecords> hot;
        Store()
        {
            cacheMemory = defaultWorkingMemory()->tryReserveWorking(
                sizeof(HotRecords), MemoryUse::Index);
            if (cacheMemory)
            {
                hot = std::make_unique<HotRecords>();
            }
        }
        static std::size_t slot(uint64_t id)
        {
            id ^= id >> 16;
            id *= 0x9e3779b97f4a7c15ULL;
            return (id ^ (id >> 32)) % 512;
        }
        RecordIndex<Record, 64> records;
        std::unordered_map<uint64_t, Source> sources;
        uint64_t freeHead = 0, live = 0;
        bool failed = false;
        static inline std::atomic<uint64_t> identities{1};
        Record get(uint64_t id)
        {
            ++threadAccesses;
            if (hot)
            {
                auto &entry = (*hot)[slot(id)];
                if (entry.id == id)
                {
                    return entry.value;
                }
                entry = {id, records[id - 1]};
                return entry.value;
            }
            return records[id - 1];
        }
        void put(uint64_t id, Record r)
        {
            ++threadAccesses;
            try
            {
                records.set(id - 1, r);
                if (hot)
                {
                    (*hot)[slot(id)] = {id, r};
                }
            }
            catch (...)
            {
                failed = true;
                throw;
            }
        }
        void retain(uint64_t id)
        {
            if (id)
            {
                auto r = get(id);
                ++r.refs;
                put(id, r);
            }
        }
        void release(uint64_t id)
        {
            if (!id)
            {
                return;
            }
            uint64_t left = 0, right = 0;
            std::shared_ptr<const AudioRevision> retired;
            {
                std::lock_guard lock(mutex);
                if (failed)
                {
                    return;
                }
                auto r = get(id);
                if (!r.refs)
                {
                    throw std::logic_error("Released edit node");
                }
                if (--r.refs)
                {
                    put(id, r);
                    return;
                }
                left = r.left;
                right = r.right;
                const auto source = r.source;
                r.left = freeHead;
                put(id, r);
                freeHead = id;
                --live;
                if (source)
                {
                    auto it = sources.find(source);
                    if (--it->second.leaves == 0)
                    {
                        retired = std::move(it->second.value);
                        sources.erase(it);
                    }
                }
            }
            // Destruction may unlink source files. Neither it nor recursive
            // reclamation holds the mutex needed by playback/viewport reads.
            retired.reset();
            release(left);
            release(right);
        }
        EditTree create(Record r,
                        std::shared_ptr<const AudioRevision> source = {})
        {
            auto a = std::make_shared<Anchor>();
            a->memory = reserveWorking(sizeof(Anchor) + 32, MemoryUse::Index);
            a->store = shared_from_this();
            std::lock_guard lock(mutex);
            if (failed)
            {
                throw std::runtime_error("Edit index storage failed");
            }
            if (source)
            {
                r.source = reinterpret_cast<uintptr_t>(source.get());
                if (!source->sourcePath().empty())
                {
                    r.representative = r.source;
                }
                auto it = sources.find(r.source);
                if (it == sources.end())
                {
                    auto memory =
                        reserveWorking(sizeof(Source) + 64, MemoryUse::Index);
                    it = sources
                             .emplace(r.source, Source{std::move(memory),
                                                       std::move(source), 0})
                             .first;
                }
                ++it->second.leaves;
            }
            if (r.left)
            {
                const auto left = get(r.left), right = get(r.right);
                r.leftFrames = left.frames;
                r.rightFrames = right.frames;
                r.leftHeight = left.height;
                r.rightHeight = right.height;
                r.leftIdentity = left.identity;
                r.rightIdentity = right.identity;
                r.representative = left.representative ? left.representative
                                                       : right.representative;
            }
            r.refs = 1;
            r.identity = identities.fetch_add(1);
            uint64_t next;
            try
            {
                if (freeHead)
                {
                    next = freeHead;
                    freeHead = get(next).left;
                    put(next, r);
                }
                else
                {
                    records.push_back(r);
                    next = records.size();
                }
                retain(r.left);
                retain(r.right);
                ++live;
            }
            catch (...)
            {
                // Never reclaim committed nodes using uncertain reference
                // counts after an interrupted write. Existing payloads remain
                // readable; the arena retires with its last reader.
                failed = true;
                throw;
            }
            a->id = next;
            EditTree result;
            result.anchor = std::move(a);
            result.id = next;
            result.identityValue = r.identity;
            result.frameCount = r.frames;
            result.depth = r.height;
            return result;
        }
    };
    EditTree::Anchor::~Anchor()
    {
        if (!id)
        {
            return;
        }
        try
        {
            store->release(id);
        }
        catch (const std::exception &e)
        {
            std::fprintf(stderr, "Edit node release: %s\n", e.what());
        }
    }
    std::shared_ptr<EditTree::Store> EditTree::storage()
    {
        static std::mutex mutex;
        static std::weak_ptr<Store> current;
        std::lock_guard lock(mutex);
        auto result = current.lock();
        if (!result)
        {
            result = std::make_shared<Store>();
            current = result;
        }
        return result;
    }
    EditTree EditTree::child(uint64_t childId) const
    {
        if (!childId)
        {
            return {};
        }
        const auto r = anchor->store->get(id);
        const bool left = childId == r.left;
        EditTree tree;
        tree.anchor = anchor;
        tree.id = childId;
        tree.identityValue = left ? r.leftIdentity : r.rightIdentity;
        tree.frameCount = left ? r.leftFrames : r.rightFrames;
        tree.depth = left ? r.leftHeight : r.rightHeight;
        return tree;
    }
    EditTree::Access EditTree::operator->() const
    {
        std::lock_guard lock(anchor->store->mutex);
        auto r = anchor->store->get(id);
        EditRange range{{}, r.channel, r.start, r.frames, r.constantValue};
        if (r.source)
        {
            range.source = anchor->store->sources.at(r.source).value;
        }
        return {{child(r.left), child(r.right), std::move(range), r.frames,
                 r.height, r.identity}};
    }
    EditTree EditTree::pin() const
    {
        if (!id || anchor->id == id)
        {
            return *this;
        }
        auto a = std::make_shared<Anchor>();
        a->memory = reserveWorking(sizeof(Anchor) + 32, MemoryUse::Index);
        a->store = anchor->store;
        {
            std::lock_guard lock(a->store->mutex);
            a->store->retain(id);
            a->id = id;
        }
        auto result = *this;
        result.anchor = std::move(a);
        return result;
    }
    std::optional<EditPeaks> EditTree::prepared() const
    {
        std::lock_guard lock(anchor->store->mutex);
        auto r = anchor->store->get(id);
        return r.prepared ? std::optional{r.peaks} : std::nullopt;
    }
    void EditTree::setPrepared(const EditPeaks &peaks) const
    {
        std::lock_guard lock(anchor->store->mutex);
        auto r = anchor->store->get(id);
        r.peaks = peaks;
        r.prepared = true;
        anchor->store->put(id, r);
    }
    EditTree EditTree::leaf(EditRange range)
    {
        Store::Record r;
        r.start = range.start;
        r.frames = range.frames;
        r.channel = range.channel;
        r.constantValue = range.constantValue;
        r.height = 1;
        return storage()->create(r, std::move(range.source));
    }
    EditTree EditTree::branch(const EditTree &left, const EditTree &right)
    {
        if (left.anchor->store != right.anchor->store)
        {
            throw std::logic_error("Foreign edit tree");
        }
        Store::Record r;
        r.left = left.id;
        r.right = right.id;
        r.frames = left.frames() + right.frames();
        r.height = 1 + std::max(left.height(), right.height());
        return left.anchor->store->create(r);
    }
    EditTree EditTree::lock(Weak weak)
    {
        auto store = storage();
        auto a = std::make_shared<Anchor>();
        a->memory = reserveWorking(sizeof(Anchor) + 32, MemoryUse::Index);
        a->store = store;
        std::lock_guard lock(store->mutex);
        if (!weak.slot || weak.slot > store->records.size())
        {
            return {};
        }
        const auto r = store->get(weak.slot);
        if (!r.refs || r.identity != weak.identity)
        {
            return {};
        }
        store->retain(weak.slot);
        a->id = weak.slot;
        EditTree result;
        result.anchor = std::move(a);
        result.id = weak.slot;
        result.identityValue = r.identity;
        result.frameCount = r.frames;
        result.depth = r.height;
        return result;
    }
    std::shared_ptr<const AudioRevision> EditTree::ownedSource() const
    {
        if (!anchor)
        {
            return {};
        }
        std::lock_guard lock(anchor->store->mutex);
        const auto source = anchor->store->get(id).representative;
        return source ? anchor->store->sources.at(source).value : nullptr;
    }
    EditTree::Stats EditTree::stats() const
    {
        if (!anchor)
        {
            return {};
        }
        std::lock_guard lock(anchor->store->mutex);
        return {anchor->store->live, anchor->store->records.size(),
                anchor->store->records.stats().residentBytes +
                    (anchor->store->hot ? sizeof(Store::HotRecords) : 0),
                anchor->store->sources.size()};
    }
} // namespace cupuacu::storage
