#include <catch2/catch_test_macros.hpp>
#include "TestPaths.hpp"
#include "audio/AudioDevices.hpp"
#include "audio/AudioCallbackCore.hpp"
#include "audio/PreparedPlayback.hpp"
#include "storage/AudioEditRevision.hpp"
#include <future>
#include <thread>
#include <chrono>

using namespace cupuacu;
using namespace std::chrono_literals;
namespace
{
    template <class Predicate> bool await(Predicate predicate)
    {
        const auto end = std::chrono::steady_clock::now() + 3s;
        do
        {
            if (predicate())
            {
                return true;
            }
            std::this_thread::sleep_for(1ms);
        } while (std::chrono::steady_clock::now() < end);
        return false;
    }
    struct SignalReader : storage::AudioReader
    {
        storage::AudioShape dimensions{65536 * 8 + 17, 2, 48000,
                                       SampleFormat::FLOAT32};
        mutable std::atomic<int> reads{0};
        mutable std::atomic<bool> callbackRead{false};
        std::thread::id callback = std::this_thread::get_id();
        storage::AudioShape shape() const override
        {
            return dimensions;
        }
        static float value(int64_t frame, int channel)
        {
            return float((frame * 31 + channel) % 8192 - 4096) / 4096;
        }
        void readChannel(int channel, int64_t start,
                         std::span<float> destination) const override
        {
            if (callback == std::this_thread::get_id())
            {
                callbackRead.store(true);
            }
            ++reads;
            validateRange(shape(), channel, start, destination.size());
            for (std::size_t i = 0; i < destination.size(); ++i)
            {
                destination[i] = value(start + i, channel);
            }
        }
    };
    bool ready(playback::ReadAhead &reader, int64_t frame)
    {
        return reader.isReady(frame);
    }
    struct Transport
    {
        int64_t position = 0;
        uint64_t start = 0, end = 0, pendingStart = 0, pendingEnd = 0,
                 missing = 0;
        bool playing = true, looping = false, pending = false;
        bool render(playback::ReadAhead &reader, std::span<float> output,
                    SelectedChannels channels = SelectedChannels::BOTH,
                    const audio::AudioProcessor *processor = nullptr)
        {
            audio::callback_core::StereoMeterLevels meter;
            return audio::callback_core::fillOutputBuffer(
                nullptr, reader.shape().channels, true, channels, position,
                start, end, looping, pending, pendingStart, pendingEnd, playing,
                output.data(), output.size() / 2, meter, processor, start, end,
                channels, &reader, &missing);
        }
    };
} // namespace

TEST_CASE(
    "Read ahead renders seeks, tiny loops, pending switches and channel "
    "selection",
    "[playback-read-ahead]")
{
    auto source = std::make_shared<SignalReader>();
    playback::ReadAhead reader(source, 4090);
    REQUIRE(await(
        [&]
        {
            return ready(reader, 4090) && ready(reader, 4100);
        }));
    Transport t{.position = 4090, .start = 4090, .end = 4100, .looping = true};
    std::array<float, 512> output;
    REQUIRE(t.render(reader, output, SelectedChannels::LEFT));
    for (int i = 0; i < 256; ++i)
    {
        REQUIRE(output[i * 2] == SignalReader::value(4090 + i % 10, 0));
        REQUIRE(output[i * 2 + 1] == 0);
    }
    t.pending = true;
    t.pendingStart = 100003;
    t.pendingEnd = 100011;
    reader.request(t.position, t.pendingStart);
    REQUIRE(await(
        [&]
        {
            return ready(reader, t.pendingStart);
        }));
    const auto oldPosition = t.position;
    REQUIRE(t.render(reader, output));
    for (int i = 0; i < 256; ++i)
    {
        const auto first = 4100 - oldPosition;
        const auto frame =
            i < first ? oldPosition + i : 100003 + (i - first) % 8;
        for (int ch = 0; ch < 2; ++ch)
        {
            REQUIRE(output[i * 2 + ch] == SignalReader::value(frame, ch));
        }
    }
    REQUIRE_FALSE(t.pending);
    REQUIRE(t.missing == 0);
    REQUIRE_FALSE(source->callbackRead.load());
    reader.close();
    REQUIRE(await(
        [&]
        {
            return reader.finished();
        }));
}

TEST_CASE(
    "Playback snapshots and processor ownership survive replacement and stop "
    "off callback",
    "[playback-read-ahead]")
{
    audio::AudioDevices device(false);
    Document doc;
    doc.initialize(SampleFormat::FLOAT32, 48000, 2, 32768);
    doc.setSample(0, 100, 0.75f);
    std::promise<std::thread::id> destroyed;
    struct Processor : audio::AudioProcessor
    {
        std::promise<std::thread::id> &destroyed;
        explicit Processor(std::promise<std::thread::id> &value)
            : destroyed(value)
        {
        }
        ~Processor()
        {
            destroyed.set_value(std::this_thread::get_id());
        }
        void process(float *, unsigned long,
                     const audio::AudioProcessContext &) const override
        {
        }
    };
    auto processor = std::make_shared<Processor>(destroyed);
    auto released = destroyed.get_future();
    audio::Play play{};
    play.document = &doc;
    play.startPos = 100;
    play.endPos = 200;
    play.previewProcessor = processor;
    REQUIRE(device.enqueue(std::move(play)));
    processor.reset();
    doc.setSample(0, 100, -0.5f);
    std::array<float, 2> output;
    device.processCallbackCycle(nullptr, output.data(), 1);
    REQUIRE(output[0] == 0.75f);
    device.enqueue(audio::Stop{});
    device.processCallbackCycle(nullptr, output.data(), 1);
    REQUIRE(released.wait_for(0ms) == std::future_status::timeout);
    device.servicePlayback();
    REQUIRE(released.wait_for(3s) == std::future_status::ready);
    REQUIRE(released.get() != std::this_thread::get_id());
    REQUIRE_FALSE(device.isPlaying());
    REQUIRE(output[0] == 0);
}

TEST_CASE(
    "Blocked read never stalls callback or stop and releases source on worker",
    "[playback-read-ahead]")
{
    struct Gated : SignalReader
    {
        std::promise<void> entered, destroyed;
        std::shared_future<void> gate;
        mutable std::atomic<bool> announced{false};
        std::shared_ptr<std::atomic<bool>> wrongThread;
        ~Gated()
        {
            wrongThread->store(callback == std::this_thread::get_id());
            destroyed.set_value();
        }
        void readChannel(int channel, int64_t start,
                         std::span<float> output) const override
        {
            if (start >= 4096 * 4)
            {
                if (!announced.exchange(true))
                {
                    const_cast<Gated *>(this)->entered.set_value();
                }
                gate.wait();
            }
            SignalReader::readChannel(channel, start, output);
        }
    };
    auto wrongThread = std::make_shared<std::atomic<bool>>(false);
    std::promise<void> release;
    auto source = std::make_shared<Gated>();
    source->wrongThread = wrongThread;
    source->gate = release.get_future().share();
    auto entered = source->entered.get_future();
    auto destroyed = source->destroyed.get_future();
    audio::AudioDevices device(false);
    audio::Play play{};
    play.readerSnapshot = source;
    play.startPos = 40000;
    play.endPos = source->shape().frames;
    REQUIRE(device.enqueue(std::move(play)));
    REQUIRE(entered.wait_for(3s) == std::future_status::ready);
    std::array<float, 512> output;
    const auto before = std::chrono::steady_clock::now();
    device.processCallbackCycle(nullptr, output.data(), 256);
    REQUIRE(std::chrono::steady_clock::now() - before < 50ms);
    REQUIRE(device.getPlaybackPosition() == 40000);
    REQUIRE(device.getPlaybackUnderrunFrames() == 0); // Initial buffering.
    for (auto value : output)
    {
        REQUIRE(value == 0);
    }
    device.enqueue(audio::Stop{});
    device.processCallbackCycle(nullptr, output.data(), 256);
    source.reset();
    device.servicePlayback();
    release.set_value();
    REQUIRE(destroyed.wait_for(3s) == std::future_status::ready);
    REQUIRE_FALSE(wrongThread->load());
    REQUIRE_FALSE(device.isPlaying());
}

TEST_CASE("Read ahead reports errors and bounds queued playback ownership",
          "[playback-read-ahead]")
{
    struct Broken : SignalReader
    {
        void readChannel(int, int64_t, std::span<float>) const override
        {
            throw std::runtime_error("read failed");
        }
    };
    audio::AudioDevices device(false);
    audio::Play play{};
    play.readerSnapshot = std::make_shared<Broken>();
    play.startPos = 0;
    play.endPos = 100;
    REQUIRE(device.enqueue(std::move(play)));
    std::array<float, 32> output;
    REQUIRE(await(
        [&]
        {
            device.processCallbackCycle(nullptr, output.data(), 16);
            return device.takePlaybackFailure();
        }));
    REQUIRE_FALSE(device.isPlaying());
    for (auto value : output)
    {
        REQUIRE(value == 0);
    }
    device.servicePlayback();
    Document doc;
    doc.initialize(SampleFormat::FLOAT32, 48000, 1, 100);
    for (int i = 0; i < 8; ++i)
    {
        audio::Play request{};
        request.document = &doc;
        request.startPos = 0;
        request.endPos = 100;
        REQUIRE(device.enqueue(std::move(request)));
    }
    audio::Play rejected{};
    rejected.document = &doc;
    rejected.startPos = 0;
    rejected.endPos = 100;
    REQUIRE_FALSE(device.enqueue(std::move(rejected)));
    REQUIRE(device.takePlaybackFailure());
    device.closeDevice();
    REQUIRE_FALSE(device.isPlaying());
}

TEST_CASE(
    "Starvation preserves source position and counts silence after playback "
    "starts",
    "[playback-read-ahead]")
{
    struct Gated : SignalReader
    {
        std::shared_future<void> release;
        void readChannel(int channel, int64_t start,
                         std::span<float> output) const override
        {
            if (start >= 40000)
            {
                release.wait();
            }
            SignalReader::readChannel(channel, start, output);
        }
    };
    std::promise<void> release;
    auto source = std::make_shared<Gated>();
    source->release = release.get_future().share();
    playback::ReadAhead reader(source);
    REQUIRE(await(
        [&]
        {
            return ready(reader, 0);
        }));
    Transport t{.end = uint64_t(source->shape().frames)};
    std::array<float, 512> output;
    REQUIRE(t.render(reader, output));
    t.position = 100000;
    REQUIRE_FALSE(t.render(reader, output));
    REQUIRE(t.position == 100000);
    REQUIRE(t.missing == 256);
    for (auto sample : output)
    {
        REQUIRE(sample == 0);
    }
    release.set_value();
    REQUIRE(await(
        [&]
        {
            return ready(reader, t.position);
        }));
    REQUIRE(t.render(reader, output));
    REQUIRE(t.position == 100256);
    for (int i = 0; i < 256; ++i)
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            REQUIRE(output[i * 2 + ch] == SignalReader::value(100000 + i, ch));
        }
    }
    reader.close();
    REQUIRE(await(
        [&]
        {
            return reader.finished();
        }));
}

TEST_CASE("Read ahead plays edited disk ranges and stops at partial-block EOF",
          "[playback-read-ahead]")
{
    const storage::AudioShape shape{65536 * 2 + 17, 1, 48000,
                                    SampleFormat::FLOAT32};
    auto store = std::make_shared<storage::AudioBlockStore>(
        test::makeUniqueTestRoot("playback") / "blocks");
    auto cache = std::make_shared<storage::DecodedBlockCache>(
        storage::AudioBlockBytes * 2);
    storage::AudioRevisionBuilder builder(shape, store, cache);
    std::vector<float> input(shape.frames);
    for (int64_t i = 0; i < shape.frames; ++i)
    {
        input[i] = SignalReader::value(i, 0);
    }
    builder.appendInterleaved(input);
    const auto original = storage::AudioEditRevision::from(builder.finish());
    storage::AudioEditTransaction edit(*original);
    edit.erase(65530, 17);
    const auto revision = edit.finish();
    playback::ReadAhead reader(revision, 65520);
    REQUIRE(await(
        [&]
        {
            return ready(reader, 65520) && ready(reader, 65776);
        }));
    Transport t{.position = 65520,
                .start = 65520,
                .end = uint64_t(revision->shape().frames)};
    std::array<float, 512> output;
    REQUIRE(t.render(reader, output));
    for (int i = 0; i < 256; ++i)
    {
        const auto frame = 65520 + i;
        const auto expected =
            SignalReader::value(frame < 65530 ? frame : frame + 17, 0);
        REQUIRE(output[i * 2] == expected);
        REQUIRE(output[i * 2 + 1] == expected);
    }
    t.position = t.end - 7;
    reader.request(t.position);
    REQUIRE(await(
        [&]
        {
            return ready(reader, t.position);
        }));
    REQUIRE(t.render(reader, output));
    REQUIRE_FALSE(t.playing);
    REQUIRE(t.position == -1);
    for (int i = 7 * 2; i < 512; ++i)
    {
        REQUIRE(output[i] == 0);
    }
    REQUIRE(cache->stats().peakResidentBytes <= storage::AudioBlockBytes * 2);
    reader.close();
    REQUIRE(await(
        [&]
        {
            return reader.finished();
        }));
}

TEST_CASE("Preview processor receives separate source spans across loop wraps",
          "[playback-read-ahead]")
{
    struct PositionProcessor : audio::AudioProcessor
    {
        void process(float *output, unsigned long count,
                     const audio::AudioProcessContext &context) const override
        {
            for (unsigned long i = 0; i < count; ++i)
            {
                output[i * 2] = float(context.bufferStartFrame + i);
            }
        }
    } processor;
    auto source = std::make_shared<SignalReader>();
    playback::ReadAhead reader(source);
    REQUIRE(await(
        [&]
        {
            return ready(reader, 10);
        }));
    Transport t{.position = 10, .start = 10, .end = 13, .looping = true};
    std::array<float, 32> output;
    REQUIRE(t.render(reader, output, SelectedChannels::BOTH, &processor));
    for (int i = 0; i < 16; ++i)
    {
        REQUIRE(output[i * 2] == 10 + i % 3);
    }
    reader.close();
    REQUIRE(await(
        [&]
        {
            return reader.finished();
        }));
}
