#pragma once
#include "WorkingMemory.hpp"
#include <map>
#include <string>
#include <type_traits>
namespace cupuacu::storage
{
    // Canonical live objects cannot be evicted without splitting revision
    // identity. Charge their handles; discard expired handles periodically.
    template <typename Key, typename Value> class WeakObjectMap
    {
        struct Entry
        {
            std::shared_ptr<void> memory;
            std::weak_ptr<Value> value;
            bool expired() const
            {
                return value.expired();
            }
        };
        std::map<Key, Entry> entries;
        std::size_t nextCleanup = 64;
        void clean()
        {
            std::erase_if(entries,
                          [](const auto &entry)
                          {
                              return entry.second.expired();
                          });
            nextCleanup = std::max<std::size_t>(64, entries.size() * 2);
        }

    public:
        std::weak_ptr<Value> &operator[](const Key &key)
        {
            if (auto found = entries.find(key); found != entries.end())
            {
                return found->second.value;
            }
            if (entries.size() >= nextCleanup)
            {
                clean();
            }
            uint64_t bytes = sizeof(Entry) + sizeof(Key) + 64;
            if constexpr (std::is_same_v<Key, std::string>)
            {
                bytes += key.size() + 1;
            }
            auto memory = defaultWorkingMemory()->tryReserveWorking(
                bytes, MemoryUse::Index);
            if (!memory)
            {
                clean();
                memory = reserveWorking(bytes, MemoryUse::Index);
            }
            return entries.emplace(key, Entry{std::move(memory), {}})
                .first->second.value;
        }
        auto begin() const
        {
            return entries.begin();
        }
        auto end() const
        {
            return entries.end();
        }
    };
} // namespace cupuacu::storage
