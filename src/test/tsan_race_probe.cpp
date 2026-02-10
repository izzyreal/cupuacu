#include <atomic>
#include <barrier>
#include <cstdint>
#include <cstdio>
#include <thread>

int main()
{
    static int64_t shared = 0;
    std::barrier syncPoint(3);

    auto writer = [&syncPoint]
    {
        syncPoint.arrive_and_wait();

        for (int i = 0; i < 1000000; ++i)
        {
            // Intentional racy read-modify-write.
            shared += 1;
            if ((i & 0xFF) == 0)
            {
                std::this_thread::yield();
            }
        }
    };

    std::thread t1(writer);
    std::thread t2(writer);
    syncPoint.arrive_and_wait();
    t1.join();
    t2.join();

    // Prevent aggressive optimization from eliding all writes.
    std::fprintf(stderr, "shared=%lld\n", static_cast<long long>(shared));
    return 0;
}
