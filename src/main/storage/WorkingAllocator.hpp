#pragma once
#include "WorkingMemory.hpp"
#include <algorithm>
#include <limits>
#include <new>
#include <vector>

namespace cupuacu::storage
{
    // Reservations belong to allocations, so vector growth accounts for both
    // old and replacement buffers and moves preserve admission automatically.
    template <typename T, MemoryUse Use> class WorkingAllocator
    {
        struct Header
        {
            std::shared_ptr<void> memory;
        };
        static constexpr auto alignment = std::max(alignof(Header), alignof(T));
        static constexpr auto prefix =
            (sizeof(Header) + alignment - 1) / alignment * alignment;

        std::shared_ptr<WorkingMemory> resource;
        template <typename, MemoryUse> friend class WorkingAllocator;

    public:
        using value_type = T;
        using is_always_equal = std::true_type;
        template <typename U> struct rebind
        {
            using other = WorkingAllocator<U, Use>;
        };
        WorkingAllocator() = default;
        explicit WorkingAllocator(std::shared_ptr<WorkingMemory> memory)
            : resource(std::move(memory))
        {
        }
        template <typename U>
        WorkingAllocator(const WorkingAllocator<U, Use> &other)
            : resource(other.resource)
        {
        }
        T *allocate(std::size_t count)
        {
            if (count > (SIZE_MAX - prefix) / sizeof(T))
            {
                throw std::bad_array_new_length();
            }
            const auto bytes = count * sizeof(T) + prefix;
            auto memory = reserveWorking(bytes, Use, resource);
            auto *base = static_cast<std::byte *>(
                ::operator new(bytes, std::align_val_t(alignment)));
            new (base) Header{std::move(memory)};
            return reinterpret_cast<T *>(base + prefix);
        }
        void deallocate(T *value, std::size_t) noexcept
        {
            auto *base = reinterpret_cast<std::byte *>(value) - prefix;
            auto *header = reinterpret_cast<Header *>(base);
            auto memory = std::move(header->memory);
            header->~Header();
            ::operator delete(base, std::align_val_t(alignment));
        }
        template <typename U>
        bool operator==(const WorkingAllocator<U, Use> &) const
        {
            return true;
        }
    };
    template <typename T, MemoryUse Use>
    class WorkingVector : public std::vector<T, WorkingAllocator<T, Use>>
    {
        using Base = std::vector<T, WorkingAllocator<T, Use>>;

    public:
        using Base::Base;
        using Base::operator=;
        WorkingVector() = default;
        WorkingVector(const std::vector<T> &other)
            : Base(other.begin(), other.end())
        {
        }
        operator std::vector<T>() const
        {
            return {this->begin(), this->end()};
        }
        friend bool operator==(const WorkingVector &a, const std::vector<T> &b)
        {
            return a.size() == b.size() &&
                   std::equal(a.begin(), a.end(), b.begin());
        }
        friend bool operator==(const WorkingVector &a, const WorkingVector &b)
        {
            return a.size() == b.size() &&
                   std::equal(a.begin(), a.end(), b.begin());
        }
    };
} // namespace cupuacu::storage
