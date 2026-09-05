#pragma once

#include "../performance/WorkMetrics.hpp"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <memory>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace cupuacu::storage
{
    // Value copies share page tables and pages. Writers detach before mutation.
    // A view borrows its owner; reading it never allocates or changes
    // refcounts.
    template <typename T, std::size_t PageElements,
              performance::Work CopyMetric>
    class PagedArray
    {
        static_assert(std::is_trivially_copyable_v<T>);
        static_assert(PageElements > 0);

    public:
        static constexpr std::size_t ELEMENTS_PER_PAGE = PageElements;
        class ReadView
        {
        public:
            ReadView() = default;
            explicit ReadView(const PagedArray &owner) : owner(&owner) {}
            std::size_t size() const
            {
                return owner ? owner->size() : 0;
            }
            bool empty() const
            {
                return size() == 0;
            }
            T operator[](std::size_t index) const
            {
                return (*owner)[index];
            }

        private:
            const PagedArray *owner = nullptr;
        };

        PagedArray() = default;
        PagedArray(const PagedArray &) = default;
        PagedArray &operator=(const PagedArray &) = default;
        PagedArray(PagedArray &&other) noexcept
            : table(std::move(other.table)),
              count(std::exchange(other.count, 0))
        {
        }
        PagedArray &operator=(PagedArray &&other) noexcept
        {
            if (this != &other)
            {
                table = std::move(other.table);
                count = std::exchange(other.count, 0);
            }
            return *this;
        }
        std::size_t size() const
        {
            return count;
        }
        bool empty() const
        {
            return count == 0;
        }
        ReadView view() const
        {
            return ReadView(*this);
        }
        T operator[](std::size_t index) const
        {
            assert(index < count);
            const auto &page = table->pages[index / PageElements];
            return page ? page->values[index % PageElements] : T{};
        }
        void set(std::size_t index, T value)
        {
            assert(index < count);
            writablePage(index / PageElements).values[index % PageElements] =
                value;
        }
        void resize(std::size_t size, T value = {})
        {
            const auto oldCount = count;
            if (size == oldCount)
            {
                return;
            }
            if (!size)
            {
                table.reset();
                count = 0;
                return;
            }
            ensurePrivateTable();
            table->pages.resize(size / PageElements +
                                (size % PageElements != 0));
            table->observe();
            count = size;
            // A partial tail may have grown or shrunk. Resize it even if it has
            // spare capacity, so truncated samples cannot reappear on regrowth.
            const auto tail =
                size > oldCount ? oldCount / PageElements : size / PageElements;
            if ((size > oldCount ? oldCount : size) % PageElements &&
                table->pages[tail])
            {
                writablePage(tail);
            }
            if (size > oldCount && value != T{})
            {
                fill(oldCount, size - oldCount, value);
            }
        }
        void assign(std::size_t size, T value)
        {
            PagedArray replacement;
            replacement.resize(size);
            if (value != T{})
            {
                replacement.fill(0, size, value);
            }
            *this = std::move(replacement);
        }
        void write(std::size_t start, const T *source, std::size_t size,
                   std::size_t stride = 1)
        {
            assert(start <= count && size <= count - start && stride > 0);
            CUPUACU_METRIC(performance::add(CopyMetric, size * sizeof(T)));
            std::size_t copied = 0;
            while (copied < size)
            {
                const auto index = start + copied;
                auto &page = writablePage(index / PageElements);
                const auto offset = index % PageElements;
                const auto length =
                    std::min(size - copied, page.values.size() - offset);
                if (stride == 1)
                {
                    std::copy_n(source + copied, length,
                                page.values.data() + offset);
                }
                else
                {
                    for (std::size_t i = 0; i < length; ++i)
                    {
                        page.values[offset + i] = source[(copied + i) * stride];
                    }
                }
                copied += length;
            }
        }
        void fill(std::size_t start, std::size_t size, T value)
        {
            assert(start <= count && size <= count - start);
            for (std::size_t done = 0; done < size;)
            {
                const auto index = start + done;
                const auto length =
                    std::min(size - done, PageElements - index % PageElements);
                auto &page = writablePage(index / PageElements);
                std::fill_n(page.values.data() + index % PageElements, length,
                            value);
                done += length;
            }
        }
        // memmove semantics, including overlap across page boundaries. Obtain
        // source pointers after detaching the destination to avoid stale
        // aliases.
        void copyWithin(std::size_t destination, std::size_t source,
                        std::size_t size)
        {
            assert(source <= count && size <= count - source);
            assert(destination <= count && size <= count - destination);
            if (source == destination || !size)
            {
                return;
            }
            CUPUACU_METRIC(performance::add(CopyMetric, size * sizeof(T)));
            const bool backward = destination > source;
            std::size_t done = 0;
            while (done < size)
            {
                const auto remaining = size - done;
                const auto src = backward ? source + remaining : source + done;
                const auto dst =
                    backward ? destination + remaining : destination + done;
                const auto srcAvailable =
                    backward ? (src - 1) % PageElements + 1
                             : PageElements - src % PageElements;
                const auto dstAvailable =
                    backward ? (dst - 1) % PageElements + 1
                             : PageElements - dst % PageElements;
                const auto length =
                    std::min({remaining, srcAvailable, dstAvailable});
                const auto from = backward ? src - length : src;
                const auto to = backward ? dst - length : dst;
                auto &target = writablePage(to / PageElements);
                const auto &origin = table->pages[from / PageElements];
                auto *output = target.values.data() + to % PageElements;
                if (origin)
                {
                    std::memmove(output,
                                 origin->values.data() + from % PageElements,
                                 length * sizeof(T));
                }
                else
                {
                    std::fill_n(output, length, T{});
                }
                done += length;
            }
        }
        PagedArray deepCopy() const
        {
            PagedArray result;
            result.resize(count);
            if (table)
            {
                for (std::size_t i = 0; i < table->pages.size(); ++i)
                {
                    const auto &source = table->pages[i];
                    if (source)
                    {
                        result.table->pages[i] =
                            std::make_shared<Page>(*source);
                    }
                    else
                    {
                        auto &page = result.writablePage(i);
                        CUPUACU_METRIC(performance::add(
                            CopyMetric, page.values.size() * sizeof(T)));
                    }
                }
            }
            return result;
        }

    private:
        struct Page
        {
            explicit Page(std::size_t size) : values(size)
            {
                observe();
            }
            Page(const Page &other) : values(other.values)
            {
                observe();
                CUPUACU_METRIC(
                    performance::add(CopyMetric, values.size() * sizeof(T)));
            }
            void resize(std::size_t size)
            {
                if (size > values.capacity())
                {
                    CUPUACU_METRIC(performance::add(CopyMetric,
                                                    values.size() * sizeof(T)));
                    values.reserve(std::min(
                        PageElements, std::max(size, values.capacity() * 2)));
                }
                values.resize(size);
                observe();
            }
            void observe()
            {
                CUPUACU_METRIC(capacity.set(values.capacity() * sizeof(T)));
            }
            std::vector<T> values;
            [[no_unique_address]] performance::Capacity capacity;
        };
        struct Table
        {
            Table() = default;
            Table(const Table &other) : pages(other.pages)
            {
                observe();
                CUPUACU_METRIC(performance::add(
                    performance::Work::MetadataBytesCopied,
                    pages.size() * sizeof(std::shared_ptr<Page>)));
            }
            void observe()
            {
                CUPUACU_METRIC(capacity.set(pages.capacity() *
                                            sizeof(std::shared_ptr<Page>)));
            }
            std::vector<std::shared_ptr<Page>> pages;
            [[no_unique_address]] performance::Capacity capacity;
        };
        void ensurePrivateTable()
        {
            if (!table)
            {
                table = std::make_shared<Table>();
            }
            else if (table.use_count() != 1)
            {
                table = std::make_shared<Table>(*table);
            }
        }
        Page &writablePage(std::size_t index)
        {
            ensurePrivateTable();
            auto &page = table->pages[index];
            const auto size =
                std::min(PageElements, count - index * PageElements);
            if (!page)
            {
                page = std::make_shared<Page>(size);
            }
            else
            {
                if (page.use_count() != 1)
                {
                    page = std::make_shared<Page>(*page);
                }
                if (page->values.size() != size)
                {
                    page->resize(size);
                }
            }
            return *page;
        }
        std::shared_ptr<Table> table;
        std::size_t count = 0;
    };
} // namespace cupuacu::storage
