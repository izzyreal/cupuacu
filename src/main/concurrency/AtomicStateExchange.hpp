#pragma once

#include "concurrency/BoundedMpmcQueue.hpp"
#include "utils/SimpleAction.hpp"

#include <atomic>
#include <functional>
#include <cstdint>
#include <array>
#include <limits>
#include <new>

namespace cupuacu::concurrency
{
    template <typename State, typename View, typename Message,
              size_t PoolSize = 3, size_t MessageQueueCapacity = 512>
    class AtomicStateExchange
    {
        using MessageQueue = BoundedMpmcQueue<Message, MessageQueueCapacity>;
        using CallbackQueue =
            BoundedMpmcQueue<utils::SimpleAction, MessageQueueCapacity>;

    protected:
        explicit AtomicStateExchange(std::function<void(State &)> reserveFn)
        {
            actions.reserve(10);
            reserveFn(activeState);

            for (auto &s : pool)
            {
                reserveFn(s);
            }

            currentSnapshot.store(&pool[0], std::memory_order_relaxed);
        }

    public:
        virtual ~AtomicStateExchange() {}

        template <class M> void enqueue(M &&msg) noexcept
        {
            queue.enqueue(Message(std::forward<M>(msg)));
        }

        void enqueueCallback(utils::SimpleAction cb) noexcept
        {
            callbackQueue.enqueue(std::move(cb));
        }

        void drainQueue() noexcept
        {
            alignas(Message) unsigned char msgBuf[sizeof(Message)];
            auto *msg = reinterpret_cast<Message *>(msgBuf);

            bool shouldPublish = false;

            while (queue.dequeue(*msg))
            {
                applyMessage(*msg);
                msg->~Message();
                shouldPublish = true;
            }

            if (shouldPublish)
            {
                publishState();
            }

            for (auto &a : actions)
            {
                a();
            }

            actions.clear();

            alignas(utils::SimpleAction) unsigned char
                cbBuf[sizeof(utils::SimpleAction)];
            auto *cb = reinterpret_cast<utils::SimpleAction *>(cbBuf);

            while (callbackQueue.dequeue(*cb))
            {
                (*cb)();
                cb->~SmallFn();
            }
        }

        View getSnapshot() const noexcept
        {
            for (;;)
            {
                State *s = currentSnapshot.load(std::memory_order_acquire);
                const auto index =
                    static_cast<std::size_t>(s - pool.data());
                auto &readers = snapshotReaders[index];
                std::uint32_t count = readers.load(std::memory_order_acquire);
                if ((count & kWriterClaim) != 0)
                {
                    continue;
                }
                if (!readers.compare_exchange_weak(
                        count, count + 1, std::memory_order_acq_rel,
                        std::memory_order_acquire))
                {
                    continue;
                }
                if (currentSnapshot.load(std::memory_order_acquire) == s)
                {
                    return View{s, &readers};
                }
                readers.fetch_sub(1, std::memory_order_release);
            }
        }

        void applyMessageImmediate(Message &&msg) noexcept
        {
            applyMessage(msg);
            publishState();

            for (auto &a : actions)
            {
                a();
            }

            actions.clear();

            alignas(utils::SimpleAction) unsigned char
                cbBuf[sizeof(utils::SimpleAction)];
            auto *cb = reinterpret_cast<utils::SimpleAction *>(cbBuf);

            while (callbackQueue.dequeue(*cb))
            {
                (*cb)();
                cb->~SmallFn();
            }
        }

    protected:
        virtual void applyMessage(const Message &msg) noexcept = 0;

        State activeState;
        std::array<State, PoolSize> pool;
        std::atomic<State *> currentSnapshot{nullptr};
        mutable std::array<std::atomic<std::uint32_t>, PoolSize>
            snapshotReaders{};
        size_t writeIndex = 0;

        std::vector<utils::SimpleAction> actions;

        void publishState() noexcept
        {
            State *const current =
                currentSnapshot.load(std::memory_order_acquire);
            for (size_t offset = 1; offset <= PoolSize; ++offset)
            {
                const size_t nextIndex = (writeIndex + offset) % PoolSize;
                State *const dst = &pool[nextIndex];
                if (dst == current)
                {
                    continue;
                }

                std::uint32_t expected = 0;
                if (!snapshotReaders[nextIndex].compare_exchange_strong(
                        expected, kWriterClaim, std::memory_order_acq_rel,
                        std::memory_order_acquire))
                {
                    continue;
                }

                *dst = activeState;
                snapshotReaders[nextIndex].store(0, std::memory_order_release);
                currentSnapshot.store(dst, std::memory_order_release);
                writeIndex = nextIndex;
                return;
            }
            droppedSnapshotPublications.fetch_add(1,
                                                  std::memory_order_relaxed);
        }

    private:
        static constexpr std::uint32_t kWriterClaim =
            std::uint32_t{1} << 31;
        std::atomic<uint64_t> droppedSnapshotPublications{0};

        MessageQueue queue;
        CallbackQueue callbackQueue;
    };

} // namespace cupuacu::concurrency
