#include "DeferredRelease.hpp"
#include <condition_variable>
#include <mutex>
#include <thread>

namespace cupuacu::concurrency
{
    namespace
    {
        struct Release
        {
            std::shared_ptr<const void> value;
            Release *next = nullptr;
        };
        class Reclaimer
        {
            std::mutex mutex;
            std::condition_variable cv;
            Release *head = nullptr, *tail = nullptr;
            bool stopping = false;
            std::thread worker;

        public:
            Reclaimer()
                : worker(
                      [this]
                      {
                          for (;;)
                          {
                              Release *release;
                              {
                                  std::unique_lock lock(mutex);
                                  cv.wait(lock,
                                          [&]
                                          {
                                              return stopping || head;
                                          });
                                  if (!head)
                                  {
                                      return;
                                  }
                                  release = head;
                                  head = head->next;
                                  if (!head)
                                  {
                                      tail = nullptr;
                                  }
                              }
                              delete release;
                          }
                      })
            {
            }
            ~Reclaimer()
            {
                {
                    std::lock_guard lock(mutex);
                    stopping = true;
                }
                cv.notify_one();
                worker.join(); // Process shutdown only; ordinary closes never
                               // wait.
            }
            void enqueue(Release *release)
            {
                {
                    std::lock_guard lock(mutex);
                    if (tail)
                    {
                        tail->next = release;
                    }
                    else
                    {
                        head = release;
                    }
                    tail = release;
                }
                cv.notify_one();
            }
        };
        Reclaimer &reclaimer()
        {
            static Reclaimer instance;
            return instance;
        }
    } // namespace
    std::shared_ptr<const void>
    retainForBackgroundRelease(std::shared_ptr<const void> value)
    {
        auto &queue = reclaimer();
        auto *release = new Release{std::move(value)};
        auto keeper = std::shared_ptr<Release>(release,
                                               [&queue](Release *node)
                                               {
                                                   queue.enqueue(node);
                                               });
        return std::shared_ptr<const void>(std::move(keeper),
                                           release->value.get());
    }
} // namespace cupuacu::concurrency
