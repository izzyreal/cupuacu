#pragma once

#include <cerrno>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <ios>
#include <stdexcept>
#include <string>

namespace cupuacu::file
{
    namespace detail
    {
        inline std::string describeErrno(const int err)
        {
            if (err == 0)
            {
                return "unknown error";
            }

            const char *message = std::strerror(err);
            if (message == nullptr || *message == '\0')
            {
                return "unknown error";
            }
            return message;
        }

        inline std::string describeErrorCode(const std::error_code &ec)
        {
            const std::string message = ec.message();
            if (!message.empty())
            {
                return message;
            }
            return "unknown error";
        }

        inline std::runtime_error makeIoFailure(const std::string &prefix,
                                                const std::string &detail)
        {
            if (detail.empty())
            {
                return std::runtime_error(prefix);
            }
            return std::runtime_error(prefix + ": " + detail);
        }

        class ScopedTemporaryFileCleanup
        {
        public:
            explicit ScopedTemporaryFileCleanup(std::filesystem::path pathToRemove)
                : path(std::move(pathToRemove))
            {
            }

            ~ScopedTemporaryFileCleanup()
            {
                if (!armed)
                {
                    return;
                }

                std::error_code ec;
                std::filesystem::remove(path, ec);
            }

            void dismiss()
            {
                armed = false;
            }

        private:
            std::filesystem::path path;
            bool armed = true;
        };
    } // namespace detail

    inline void ensureParentDirectoryExists(const std::filesystem::path &path)
    {
        const auto parentPath = path.parent_path();
        if (parentPath.empty())
        {
            return;
        }

        std::error_code ec;
        std::filesystem::create_directories(parentPath, ec);
        if (ec)
        {
            throw detail::makeIoFailure("Failed to create output directory",
                                        detail::describeErrorCode(ec));
        }
    }

    inline std::filesystem::path
    makeTemporarySiblingPath(const std::filesystem::path &destination)
    {
        const auto tick = static_cast<unsigned long long>(
            std::chrono::steady_clock::now().time_since_epoch().count());

        for (int attempt = 0; attempt < 64; ++attempt)
        {
            auto candidate = destination;
            candidate += ".cupuacu.tmp-" + std::to_string(tick) + "-" +
                         std::to_string(attempt);

            std::error_code ec;
            if (!std::filesystem::exists(candidate, ec) && !ec)
            {
                return candidate;
            }
        }

        throw detail::makeIoFailure("Failed to allocate temporary output path",
                                    "could not find an unused sibling temp name");
    }

    inline void replaceFile(const std::filesystem::path &source,
                            const std::filesystem::path &destination)
    {
        std::error_code ec;
        if (std::filesystem::exists(destination, ec))
        {
            std::filesystem::remove(destination, ec);
            if (ec)
            {
                throw detail::makeIoFailure("Failed to replace output file",
                                            detail::describeErrorCode(ec));
            }
        }
        else if (ec)
        {
            throw detail::makeIoFailure("Failed to query output file",
                                        detail::describeErrorCode(ec));
        }

        std::filesystem::rename(source, destination, ec);
        if (ec)
        {
            throw detail::makeIoFailure("Failed to replace output file",
                                        detail::describeErrorCode(ec));
        }
    }

    template <typename Writer>
    inline void writeFileAtomically(const std::filesystem::path &destination,
                                    Writer &&writer)
    {
        ensureParentDirectoryExists(destination);
        const auto temporaryPath = makeTemporarySiblingPath(destination);
        detail::ScopedTemporaryFileCleanup cleanup(temporaryPath);

        writer(temporaryPath);
        replaceFile(temporaryPath, destination);
        cleanup.dismiss();
    }
} // namespace cupuacu::file
