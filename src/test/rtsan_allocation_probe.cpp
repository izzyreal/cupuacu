#include <rtsan_standalone/rtsan_standalone.h>

#include <vector>

int main()
{
    __rtsan::Initialize();

    __rtsan::ScopedSanitizeRealtime realtimeScope;
    const std::vector<int> allocation{1};
    return allocation.front() - 1;
}
