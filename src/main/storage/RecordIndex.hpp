#pragma once
#include "WorkingMemory.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace cupuacu::storage
{
    // Append-only working index, shared by builders and published readers.
    // Small indexes stay resident; larger ones retain only a write tail and
    // one read page. The anonymous file is released with its last owner.
    // This is process-local storage, never an on-disk interchange format.
    // All access (including last release) belongs to workers.
    template <class T, std::size_t PageElements = 256> class RecordIndex
    {
        static_assert(std::is_trivially_copyable_v<T>);
        struct Data
        {
            mutable std::mutex mutex;
            std::shared_ptr<WorkingMemory> memory;
            std::shared_ptr<void> tailMemory, pageMemory;
            std::FILE *file = nullptr;
            std::vector<T> tail, page;
            uint64_t stored = 0, pageStart = UINT64_MAX;
            uint64_t readBytes = 0, writtenBytes = 0;
            bool sealed = false, failed = false;
            explicit Data(std::shared_ptr<WorkingMemory> resource)
                : memory(std::move(resource))
            {
            }
            bool allocate(std::vector<T> &buffer, std::shared_ptr<void> &lease,
                          uint64_t capacity)
            {
                if (!memory)
                {
                    memory = defaultWorkingMemory();
                }
                auto next = memory->tryReserveWorking(capacity * sizeof(T),
                                                      MemoryUse::Index);
                if (!next)
                {
                    return false;
                }
                std::vector<T> replacement;
                replacement.reserve(capacity);
                replacement.assign(buffer.begin(), buffer.end());
                buffer.swap(replacement);
                std::vector<T>().swap(replacement);
                lease = std::move(next);
                return true;
            }
            ~Data()
            {
                if (file)
                {
                    std::fclose(file);
                }
            }
            void seek(uint64_t index)
            {
                if (index > uint64_t(INT64_MAX) / sizeof(T))
                {
                    throw std::overflow_error("Index offset overflow");
                }
#ifdef _WIN32
                const auto error = _fseeki64(file, index * sizeof(T), SEEK_SET);
#else
                const auto error =
                    fseeko(file, off_t(index * sizeof(T)), SEEK_SET);
#endif
                if (error)
                {
                    throw std::runtime_error("Index seek failed");
                }
            }
            void flushTail()
            {
                if (!file)
                {
                    file = std::tmpfile();
                    if (!file)
                    {
                        throw std::runtime_error("Cannot create working index");
                    }
                    std::setvbuf(file, nullptr, _IONBF, 0);
                }
                seek(stored);
                if (std::fwrite(tail.data(), sizeof(T), tail.size(), file) !=
                    tail.size())
                {
                    failed = true;
                    throw std::runtime_error("Working index write failed");
                }
                writtenBytes += tail.size() * sizeof(T);
                stored += tail.size();
                tail.clear();
                pageStart = UINT64_MAX;
            }
            void writeOne(T value)
            {
                flushTail();
                seek(stored);
                if (std::fwrite(&value, sizeof(T), 1, file) != 1)
                {
                    failed = true;
                    throw std::runtime_error("Working index write failed");
                }
                ++stored;
                writtenBytes += sizeof(T);
            }
            T at(uint64_t index)
            {
                if (index >= stored)
                {
                    return tail.at(std::size_t(index - stored));
                }
                const auto first = index / PageElements * PageElements;
                if (pageStart != first)
                {
                    pageStart = UINT64_MAX;
                    if (page.capacity() < PageElements &&
                        !allocate(page, pageMemory, PageElements))
                    {
                        T value;
                        seek(index);
                        if (std::fread(&value, sizeof(T), 1, file) != 1)
                        {
                            throw std::runtime_error(
                                "Working index read failed");
                        }
                        readBytes += sizeof(T);
                        return value;
                    }
                    page.resize(
                        std::min<uint64_t>(PageElements, stored - first));
                    seek(first);
                    if (std::fread(page.data(), sizeof(T), page.size(), file) !=
                        page.size())
                    {
                        throw std::runtime_error("Working index read failed");
                    }
                    readBytes += page.size() * sizeof(T);
                    pageStart = first;
                }
                return page[index - first];
            }
        };
        std::shared_ptr<Data> data;

    public:
        explicit RecordIndex(std::shared_ptr<WorkingMemory> memory = {})
            : data(std::make_shared<Data>(std::move(memory)))
        {
        }
        uint64_t size() const
        {
            std::lock_guard lock(data->mutex);
            return data->stored + data->tail.size();
        }
        bool empty() const
        {
            return size() == 0;
        }
        T operator[](uint64_t index) const
        {
            std::lock_guard lock(data->mutex);
            return data->at(index);
        }
        T back() const
        {
            std::lock_guard lock(data->mutex);
            if (!data->tail.empty())
            {
                return data->tail.back();
            }
            if (!data->stored)
            {
                throw std::out_of_range("Empty working index");
            }
            return data->at(data->stored - 1);
        }
        void push_back(T value)
        {
            std::lock_guard lock(data->mutex);
            if (data->sealed || data->failed)
            {
                throw std::logic_error(
                    "Cannot append to a sealed or failed index");
            }
            if (data->tail.size() == PageElements)
            {
                data->flushTail();
            }
            if (data->tail.size() == data->tail.capacity() &&
                !data->allocate(
                    data->tail, data->tailMemory,
                    std::min<uint64_t>(
                        PageElements,
                        std::max<uint64_t>(1, data->tail.size() * 2))))
            {
                data->writeOne(value);
                return;
            }
            data->tail.push_back(value);
        }
        void setBack(T value)
        {
            std::lock_guard lock(data->mutex);
            if (data->sealed || data->failed ||
                (data->tail.empty() && !data->stored))
            {
                throw std::logic_error("Cannot update working index tail");
            }
            if (!data->tail.empty())
            {
                data->tail.back() = value;
            }
            else
            {
                data->seek(data->stored - 1);
                if (std::fwrite(&value, sizeof(T), 1, data->file) != 1)
                {
                    data->failed = true;
                    throw std::runtime_error("Working index update failed");
                }
                data->pageStart = UINT64_MAX;
            }
        }
        void set(uint64_t index, T value)
        {
            std::lock_guard lock(data->mutex);
            if (data->sealed || data->failed)
            {
                throw std::logic_error("Cannot update sealed or failed index");
            }
            if (index >= data->stored)
            {
                data->tail.at(index - data->stored) = value;
                return;
            }
            data->seek(index);
            if (std::fwrite(&value, sizeof(T), 1, data->file) != 1)
            {
                data->failed = true;
                throw std::runtime_error("Working index update failed");
            }
            if (data->pageStart != UINT64_MAX && index >= data->pageStart &&
                index - data->pageStart < data->page.size())
            {
                data->page[index - data->pageStart] = value;
            }
        }
        void seal()
        {
            std::lock_guard lock(data->mutex);
            if (data->file && std::fflush(data->file))
            {
                data->failed = true;
                throw std::runtime_error("Working index flush failed");
            }
            if (data->failed)
            {
                throw std::runtime_error("Working index write failed");
            }
            data->sealed = true;
        }
        struct Stats
        {
            uint64_t records, residentBytes, diskBytes, readBytes;
        };
        Stats stats() const
        {
            std::lock_guard lock(data->mutex);
            return {data->stored + data->tail.size(),
                    (data->tail.capacity() + data->page.capacity()) * sizeof(T),
                    data->writtenBytes, data->readBytes};
        }
        // Values, rather than references, prevent cache eviction from
        // invalidating a caller's current record.
        struct Iterator
        {
            const RecordIndex *owner;
            uint64_t index;
            T operator*() const
            {
                return (*owner)[index];
            }
            Iterator &operator++()
            {
                ++index;
                return *this;
            }
            bool operator!=(const Iterator &other) const
            {
                return index != other.index;
            }
        };
        Iterator begin() const
        {
            return {this, 0};
        }
        Iterator end() const
        {
            return {this, size()};
        }
    };
} // namespace cupuacu::storage
