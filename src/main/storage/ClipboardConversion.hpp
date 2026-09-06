#pragma once
#include "../ClipboardAudio.hpp"
#include "../concurrency/ScheduledLatestValue.hpp"
#include "../concurrency/DeferredRelease.hpp"
#include <filesystem>

namespace cupuacu::storage
{
    // The resident destination keeps the legacy full-segment representation.
    // Disk conversion uses bounded sample scratch plus compressed provenance.
    ClipboardAudio convertClipboard(const ClipboardAudio &, bool toRevision,
                                    const std::filesystem::path &,
                                    const std::function<bool()> &cancel);
    class ClipboardConversion
    {
        using Worker =
            concurrency::ScheduledLatestValue<int,
                                              std::shared_ptr<ClipboardAudio>>;
        Worker worker;

    public:
        uint64_t operationId = 0, tabId = 0, documentVersion = 0,
                 clipboardVersion = 0;
        int64_t cursor = 0, start = 0, end = -1;
        bool selected = false;
        ClipboardConversion(
            ClipboardAudio clip, bool toRevision, std::filesystem::path path,
            std::shared_ptr<concurrency::TaskScheduler> scheduler = {})
            : worker(
                  [clip = std::move(clip), toRevision, path = std::move(path)](
                      int, const Worker::CancelCheck &cancel)
                      -> std::optional<std::shared_ptr<ClipboardAudio>>
                  {
                      if (cancel())
                      {
                          return {};
                      }
                      return concurrency::releaseOnWorker(
                          std::make_shared<ClipboardAudio>(convertClipboard(
                              clip, toRevision, path, cancel)));
                  },
                  std::move(scheduler))
        {
            worker.submit(0);
        }
        auto takePublished()
        {
            return worker.takePublished();
        }
        void close()
        {
            worker.close();
        }
        void waitUntilClosed()
        {
            worker.waitUntilClosed();
        }
    };
} // namespace cupuacu::storage
