#pragma once

#include "../performance/WorkMetrics.hpp"

#include <algorithm>
#include <cassert>
#include <compare>
#include <cstddef>
#include <iterator>
#include <memory>
#include <utility>
#include <vector>

namespace cupuacu::gui
{
    struct Peak
    {
        float min;
        float max;
    };

    // Copies share a page table and its peak pages. Mutation never exposes a
    // writable reference, so snapshots cannot be modified through aliases.
    // Separate copies may be read/written concurrently; the same instance may
    // not.
    class PeakLevel
    {
    public:
        static constexpr std::size_t PEAKS_PER_PAGE = 1024;

        PeakLevel() = default;
        PeakLevel(const PeakLevel &) = default;
        PeakLevel &operator=(const PeakLevel &) = default;
        PeakLevel(PeakLevel &&other) noexcept
            : directory(std::move(other.directory)),
              count(std::exchange(other.count, 0))
        {
        }
        PeakLevel &operator=(PeakLevel &&other) noexcept
        {
            if (this != &other)
            {
                directory = std::move(other.directory);
                count = std::exchange(other.count, 0);
            }
            return *this;
        }

        class ConstIterator
        {
        public:
            using iterator_category = std::random_access_iterator_tag;
            using value_type = Peak;
            using difference_type = std::ptrdiff_t;
            using pointer = const Peak *;
            using reference = const Peak &;

            ConstIterator() = default;
            ConstIterator(const PeakLevel *owner, difference_type index)
                : owner(owner), index(index)
            {
            }
            reference operator*() const
            {
                return (*owner)[index];
            }
            pointer operator->() const
            {
                return &**this;
            }
            reference operator[](difference_type offset) const
            {
                return *(*this + offset);
            }
            ConstIterator &operator++()
            {
                ++index;
                return *this;
            }
            ConstIterator operator++(int)
            {
                auto old = *this;
                ++*this;
                return old;
            }
            ConstIterator &operator--()
            {
                --index;
                return *this;
            }
            ConstIterator operator--(int)
            {
                auto old = *this;
                --*this;
                return old;
            }
            ConstIterator &operator+=(difference_type offset)
            {
                index += offset;
                return *this;
            }
            ConstIterator &operator-=(difference_type offset)
            {
                index -= offset;
                return *this;
            }
            friend ConstIterator operator+(ConstIterator it, difference_type n)
            {
                return it += n;
            }
            friend ConstIterator operator+(difference_type n, ConstIterator it)
            {
                return it += n;
            }
            friend ConstIterator operator-(ConstIterator it, difference_type n)
            {
                return it -= n;
            }
            friend difference_type operator-(ConstIterator a, ConstIterator b)
            {
                return a.index - b.index;
            }
            auto operator<=>(const ConstIterator &) const = default;

        private:
            const PeakLevel *owner = nullptr;
            difference_type index = 0;
        };

        [[nodiscard]] std::size_t size() const
        {
            return count;
        }
        [[nodiscard]] bool empty() const
        {
            return count == 0;
        }
        [[nodiscard]] ConstIterator begin() const
        {
            return {this, 0};
        }
        [[nodiscard]] ConstIterator end() const
        {
            return {this, static_cast<std::ptrdiff_t>(count)};
        }

        const Peak &operator[](std::size_t index) const
        {
            assert(index < count);
            const auto &page = directory->pages[index / PEAKS_PER_PAGE];
            return page ? page->peaks[index % PEAKS_PER_PAGE] : zero;
        }

        void set(std::size_t index, Peak value)
        {
            assert(index < count);
            if (directory.use_count() != 1)
            {
                directory = std::make_shared<Directory>(*directory);
            }
            auto &page = directory->pages[index / PEAKS_PER_PAGE];
            if (!page)
            {
                page = std::make_shared<Page>(
                    std::min(PEAKS_PER_PAGE, count - index / PEAKS_PER_PAGE * PEAKS_PER_PAGE));
            }
            else if (page.use_count() != 1)
            {
                page = std::make_shared<Page>(*page);
            }
            page->peaks[index % PEAKS_PER_PAGE] = value;
        }

        // Structural edits may resize the table, but retain unchanged pages.
        // New space is zero-initialized, including a previously truncated tail.
        void resize(std::size_t newCount)
        {
            if (newCount == count)
            {
                return;
            }
            if (newCount == 0)
            {
                directory.reset();
                count = 0;
                return;
            }
            const auto pageCount =
                newCount / PEAKS_PER_PAGE + (newCount % PEAKS_PER_PAGE != 0);
            auto replacement = std::make_shared<Directory>(pageCount);
            if (directory)
            {
                const auto retained =
                    std::min(pageCount, directory->pages.size());
                for (std::size_t i = 0; i < retained; ++i)
                {
                    const auto &page = directory->pages[i];
                    const auto oldSize =
                        std::min(PEAKS_PER_PAGE, count - i * PEAKS_PER_PAGE);
                    const auto newSize =
                        std::min(PEAKS_PER_PAGE, newCount - i * PEAKS_PER_PAGE);
                    if (page && newSize > oldSize)
                    {
                        auto grown = std::make_shared<Page>(newSize);
                        std::copy_n(page->peaks.begin(), oldSize,
                                    grown->peaks.begin());
                        CUPUACU_METRIC(
                            performance::add(performance::Work::PeakBytesCopied,
                                             oldSize * sizeof(Peak)));
                        replacement->pages[i] = std::move(grown);
                    }
                    else
                    {
                        replacement->pages[i] = page;
                    }
                }
                CUPUACU_METRIC(
                    performance::add(performance::Work::MetadataBytesCopied,
                                     retained * sizeof(std::shared_ptr<Page>)));
            }
            directory = std::move(replacement);
            count = newCount;
        }

    private:
        struct Page
        {
            explicit Page(std::size_t size) : peaks(size)
            {
                CUPUACU_METRIC(
                    observedCapacity.set(peaks.capacity() * sizeof(Peak)));
            }
            Page(const Page &other) : peaks(other.peaks)
            {
                CUPUACU_METRIC(
                    observedCapacity.set(peaks.capacity() * sizeof(Peak)));
                CUPUACU_METRIC(
                    performance::add(performance::Work::PeakBytesCopied,
                                     peaks.size() * sizeof(Peak)));
            }
            std::vector<Peak> peaks;
            [[no_unique_address]] performance::Capacity observedCapacity;
        };
        struct Directory
        {
            explicit Directory(std::size_t size) : pages(size)
            {
                CUPUACU_METRIC(observedCapacity.set(
                    pages.capacity() * sizeof(std::shared_ptr<Page>)));
            }
            Directory(const Directory &other) : pages(other.pages)
            {
                CUPUACU_METRIC(observedCapacity.set(
                    pages.capacity() * sizeof(std::shared_ptr<Page>)));
                CUPUACU_METRIC(performance::add(
                    performance::Work::MetadataBytesCopied,
                    pages.size() * sizeof(std::shared_ptr<Page>)));
            }
            std::vector<std::shared_ptr<Page>> pages;
            [[no_unique_address]] performance::Capacity observedCapacity;
        };
        std::shared_ptr<Directory> directory;
        std::size_t count = 0;
        inline static const Peak zero{0.0f, 0.0f};
    };
} // namespace cupuacu::gui
