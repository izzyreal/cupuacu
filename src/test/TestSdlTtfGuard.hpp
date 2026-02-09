#pragma once

namespace cupuacu::test
{
    class SdlTtfGuard
    {
    public:
        SdlTtfGuard();
        ~SdlTtfGuard();
    };

    void ensureSdlTtfInitialized();
} // namespace cupuacu::test
