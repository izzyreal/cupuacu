#pragma once

#include "Paths.hpp"
#include "State.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>
#include <typeinfo>

namespace cupuacu::test
{
    inline std::filesystem::path makeUniqueTestRoot(
        const std::string_view label = "state")
    {
        static std::atomic<uint64_t> counter{0};

        std::string sanitized(label);
        std::transform(sanitized.begin(), sanitized.end(), sanitized.begin(),
                       [](const unsigned char ch)
                       {
                           return std::isalnum(ch) != 0 ? static_cast<char>(ch)
                                                        : '-';
                       });
        if (sanitized.empty())
        {
            sanitized = "state";
        }

        const auto now = std::chrono::high_resolution_clock::now()
                             .time_since_epoch()
                             .count();
        const auto uniqueSuffix = counter.fetch_add(1);
        return std::filesystem::temp_directory_path() / "cupuacu-tests" /
               (sanitized + "-" + std::to_string(now) + "-" +
                std::to_string(uniqueSuffix));
    }

    class TestPaths : public cupuacu::Paths
    {
    public:
        explicit TestPaths(std::filesystem::path rootToUse)
            : root(std::move(rootToUse))
        {
            std::error_code ec;
            std::filesystem::create_directories(appConfigHome(), ec);
            std::filesystem::create_directories(appDocumentsPath(), ec);
        }

    protected:
        std::filesystem::path appConfigHome() const override
        {
            return root / "config-home";
        }

        std::filesystem::path appDocumentsPath() const override
        {
            return root / "documents";
        }

    private:
        std::filesystem::path root;
    };

    inline void installTestPaths(cupuacu::State &state,
                                 const std::string_view label = "state")
    {
        state.paths = std::make_unique<TestPaths>(makeUniqueTestRoot(label));
    }

    inline void installTestPaths(cupuacu::State &state,
                                 std::filesystem::path root)
    {
        state.paths = std::make_unique<TestPaths>(std::move(root));
    }

    inline void ensureTestPaths(cupuacu::State &state,
                                const std::string_view label = "state")
    {
        const auto *paths = state.paths.get();
        if (paths == nullptr || typeid(*paths) == typeid(cupuacu::Paths))
        {
            installTestPaths(state, label);
        }
    }

    struct StateWithTestPaths : cupuacu::State
    {
        explicit StateWithTestPaths(const std::string_view label = "state")
        {
            installTestPaths(*this, label);
        }

        explicit StateWithTestPaths(std::filesystem::path root)
        {
            installTestPaths(*this, std::move(root));
        }
    };
} // namespace cupuacu::test
