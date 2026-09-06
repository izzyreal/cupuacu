#pragma once
#include "RecordIndex.hpp"
#include <optional>

namespace cupuacu::storage
{
    // Worker-owned mutable AVL index. Both records and lookup buffers are
    // bounded by the working-memory resource, independent of key count.
    template <typename Value> class WorkingMap
    {
        struct Entry
        {
            uint64_t key = 0, left = 0, right = 0;
            int height = 1;
            Value value{};
        };
        RecordIndex<Entry, 64> entries;
        uint64_t root = 0;
        Entry get(uint64_t id) const
        {
            return entries[id - 1];
        }
        int height(uint64_t id) const
        {
            return id ? get(id).height : 0;
        }
        void put(uint64_t id, Entry e)
        {
            e.height = 1 + std::max(height(e.left), height(e.right));
            entries.set(id - 1, e);
        }
        uint64_t rotate(uint64_t id, bool left)
        {
            auto a = get(id);
            const auto next = left ? a.right : a.left;
            auto b = get(next);
            if (left)
            {
                a.right = b.left;
                b.left = id;
            }
            else
            {
                a.left = b.right;
                b.right = id;
            }
            put(id, a);
            put(next, b);
            return next;
        }
        uint64_t insert(uint64_t id, uint64_t key, Value value)
        {
            if (!id)
            {
                entries.push_back({key, 0, 0, 1, value});
                return entries.size();
            }
            auto e = get(id);
            if (key == e.key)
            {
                e.value = value;
                entries.set(id - 1, e);
                return id;
            }
            if (key < e.key)
            {
                e.left = insert(e.left, key, value);
            }
            else
            {
                e.right = insert(e.right, key, value);
            }
            put(id, e);
            const auto balance = height(e.left) - height(e.right);
            if (balance > 1)
            {
                if (key > get(e.left).key)
                {
                    e.left = rotate(e.left, true);
                    put(id, e);
                }
                return rotate(id, false);
            }
            if (balance < -1)
            {
                if (key < get(e.right).key)
                {
                    e.right = rotate(e.right, false);
                    put(id, e);
                }
                return rotate(id, true);
            }
            return id;
        }

    public:
        std::optional<Value> find(uint64_t key) const
        {
            for (auto id = root; id;)
            {
                const auto e = get(id);
                if (e.key == key)
                {
                    return e.value;
                }
                id = key < e.key ? e.left : e.right;
            }
            return {};
        }
        void set(uint64_t key, Value value)
        {
            root = insert(root, key, value);
        }
        void clear()
        {
            entries = RecordIndex<Entry, 64>();
            root = 0;
        }
        auto stats() const
        {
            return entries.stats();
        }
    };
} // namespace cupuacu::storage
