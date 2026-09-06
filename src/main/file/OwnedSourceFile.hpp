#pragma once

#include "FileIo.hpp"
#include "../storage/AudioRevision.hpp"
#include <fstream>
#ifdef __APPLE__
#include <sys/clonefile.h>
#endif

namespace cupuacu::file
{
    // Both paths are independent files. Call on a worker; fallback scratch is
    // bounded and cancellation is checked before publication by the caller.
    inline bool
    cloneOrCopySource(const std::filesystem::path &source,
                      const std::filesystem::path &destination,
                      const std::function<void(double)> &progress = {},
                      bool preferClone = true)
    {
        if (progress)
        {
            progress(0);
        }
#ifdef __APPLE__
        if (preferClone &&
            clonefile(source.c_str(), destination.c_str(), 0) == 0)
        {
            if (progress)
            {
                progress(1);
            }
            return true;
        }
#endif
        const auto size = std::filesystem::file_size(source);
        std::ifstream input(source, std::ios::binary);
        std::ofstream output(destination, std::ios::binary | std::ios::trunc);
        if (!input || !output)
        {
            throw std::runtime_error("Cannot copy owned source");
        }
        std::array<char, 65536> buffer;
        uint64_t copied = 0;
        while (copied < size)
        {
            const auto count = std::min<uint64_t>(buffer.size(), size - copied);
            input.read(buffer.data(), std::streamsize(count));
            output.write(buffer.data(), std::streamsize(count));
            if (!input || !output)
            {
                throw std::runtime_error("Owned source copy failed");
            }
            copied += count;
            if (progress)
            {
                progress(double(copied) / size);
            }
        }
        output.close();
        if (!output)
        {
            throw std::runtime_error("Owned source close failed");
        }
        return false;
    }

    // Writes the new container once, owns it before replacing the destination,
    // and retains it without decoding or rebasing the document/history roots.
    template <class Writer>
    std::shared_ptr<const storage::AudioRevision> writeOwnedRevisionContainer(
        const std::filesystem::path &destination,
        const std::filesystem::path &workingRoot, storage::AudioShape shape,
        Writer writer, const std::function<void(double)> &progress = {})
    {
        static std::atomic<uint64_t> next{1};
        auto store = std::make_shared<storage::AudioBlockStore>(
            workingRoot /
            ("saved-" +
             std::to_string(
                 std::chrono::steady_clock::now().time_since_epoch().count()) +
             "-" + std::to_string(next.fetch_add(1))));
        const auto owned =
            store->path() / ("source" + destination.extension().string());
        writer(owned);
        shape.frames =
            0; // Container ownership only; the edit root supplies audio.
        storage::AudioRevisionBuilder builder(
            shape, store, std::make_shared<storage::DecodedBlockCache>(0));
        auto reference = builder.finish(owned);
        writeFileAtomically(destination,
                            [&](const auto &temporary)
                            {
                                cloneOrCopySource(owned, temporary, progress);
                                if (progress)
                                {
                                    progress(1);
                                }
                            });
        return reference;
    }
} // namespace cupuacu::file
