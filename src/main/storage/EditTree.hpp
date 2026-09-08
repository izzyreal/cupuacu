#pragma once
#include "AudioRevision.hpp"
#include <optional>
#include <unordered_map>

namespace cupuacu::storage
{
    struct EditRange
    {
        std::shared_ptr<const AudioRevision> source;
        int channel = 0;
        int64_t start = 0, frames = 0;
        float constantValue = 0;
    };
    struct EditPeaks
    {
        waveform::Peak whole = waveform::emptyPeak();
        waveform::Peak head = waveform::emptyPeak();
        waveform::Peak tail = waveform::emptyPeak();
        int64_t headFrames = 0, tailFrames = 0;
    };
    // Disk records own child references. In-memory handles retain only a root,
    // never a recursively resident subtree. All access/reclamation is
    // worker-only.
    class EditTree
    {
        struct Store;
        struct Anchor
        {
            std::shared_ptr<void> memory;
            std::shared_ptr<Store> store;
            uint64_t id = 0;
            ~Anchor();
        };
        std::shared_ptr<Anchor> anchor;
        uint64_t id = 0, identityValue = 0;
        int64_t frameCount = 0;
        int depth = 0;
        static std::shared_ptr<Store> storage();
        EditTree child(uint64_t) const;

    public:
        struct Node;
        struct Access;
        EditTree() = default;
        explicit operator bool() const
        {
            return id != 0;
        }
        uint64_t identity() const
        {
            return identityValue;
        }
        int64_t frames() const
        {
            return frameCount;
        }
        int height() const
        {
            return depth;
        }
        Access operator->() const;
        EditTree pin() const;
        std::optional<EditPeaks> prepared() const;
        void setPrepared(const EditPeaks &) const;
        static EditTree leaf(EditRange);
        static EditTree branch(const EditTree &, const EditTree &);
        struct Stats
        {
            uint64_t liveNodes, allocatedRecords, residentBytes, liveSources;
        };
        Stats stats() const;
        std::shared_ptr<const AudioRevision> ownedSource() const;
        struct Weak
        {
            uint64_t slot = 0, identity = 0;
        };
        Weak weak() const
        {
            return {id, identityValue};
        }
        static EditTree lock(Weak);
        static inline thread_local uint64_t threadAccesses = 0;
    };
    struct EditTree::Node
    {
        EditTree left, right;
        EditRange range;
        int64_t frames;
        int height;
        uint64_t identity;
    };
    struct EditTree::Access
    {
        Node value;
        const Node *operator->() const
        {
            return &value;
        }
    };
} // namespace cupuacu::storage
