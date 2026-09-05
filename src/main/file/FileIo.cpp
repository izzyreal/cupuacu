#include "FileIo.hpp"
#ifdef _WIN32
#include <windows.h>
#endif

void cupuacu::file::replaceFile(const std::filesystem::path &source,
                                const std::filesystem::path &destination)
{
#ifdef _WIN32
    if (!MoveFileExW(source.c_str(), destination.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        const std::error_code ec(static_cast<int>(GetLastError()),
                                 std::system_category());
        throw detail::makeIoFailure("Failed to replace output file",
                                    ec.message());
    }
#else
    std::error_code ec;
    std::filesystem::rename(source, destination, ec);
    if (ec)
    {
        throw detail::makeIoFailure("Failed to replace output file",
                                    ec.message());
    }
#endif
}
