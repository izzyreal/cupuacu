#pragma once

#include "EditTree.hpp"
#include <utility>
#include <optional>

namespace cupuacu::storage
{
    // A persistent AVL sequence of ranges in immutable imported block
    // directories. Leaves never reference edited revisions, so history cannot
    // create recursive reader chains. Final release remains worker-only.
    class AudioEditRevision final : public AudioReader
    {
    public:
        using SourceRange = EditRange;
        struct PeakWork
        {
            uint64_t visitedNodes = 0;
            uint64_t preparedNodes = 0;
            uint64_t boundarySamples = 0;
            uint64_t sourcePeaks = 0;
        };

    private:
        friend class AudioEditTransaction;
        friend class RevisionArchive;
        static inline std::atomic<uint64_t> nextIdentity{1};
        const uint64_t identity = nextIdentity.fetch_add(1);
        using Tree = EditTree;
        using PreparedPeaks = EditPeaks;
        AudioShape dimensions;
        std::shared_ptr<void> rootMemory;
        std::shared_ptr<const AudioRevision> originalSource;
        std::vector<Tree> channels;

        static int height(const Tree &tree)
        {
            return tree.height();
        }
        static int64_t length(const Tree &tree)
        {
            return tree.frames();
        }
        static int64_t sum(int64_t a, int64_t b)
        {
            if (b > INT64_MAX - a)
            {
                throw std::overflow_error("Audio sequence length overflow");
            }
            return a + b;
        }
        static Tree leaf(SourceRange range, uint64_t &allocated)
        {
            if (!range.frames)
            {
                return {};
            }
            const auto frames = range.frames;
            ++allocated;
            return Tree::leaf(std::move(range));
        }
        static Tree branch(Tree left, Tree right, uint64_t &allocated)
        {
            if (!left)
            {
                return right;
            }
            if (!right)
            {
                return left;
            }
            const auto frames = sum(left->frames, right->frames);
            const auto depth = 1 + std::max(left->height, right->height);
            ++allocated;
            return Tree::branch(left, right);
        }
        static Tree balance(Tree left, Tree right, uint64_t &allocated)
        {
            if (height(left) > height(right) + 1)
            {
                if (height(left->left) >= height(left->right))
                {
                    return branch(left->left,
                                  branch(left->right, right, allocated),
                                  allocated);
                }
                const auto middle = left->right;
                return branch(branch(left->left, middle->left, allocated),
                              branch(middle->right, right, allocated),
                              allocated);
            }
            if (height(right) > height(left) + 1)
            {
                if (height(right->right) >= height(right->left))
                {
                    return branch(branch(left, right->left, allocated),
                                  right->right, allocated);
                }
                const auto middle = right->left;
                return branch(branch(left, middle->left, allocated),
                              branch(middle->right, right->right, allocated),
                              allocated);
            }
            return branch(std::move(left), std::move(right), allocated);
        }
        static Tree join(Tree left, Tree right, uint64_t &allocated)
        {
            if (!left)
            {
                return right;
            }
            if (!right)
            {
                return left;
            }
            if (height(left) > height(right) + 1)
            {
                return balance(left->left, join(left->right, right, allocated),
                               allocated);
            }
            if (height(right) > height(left) + 1)
            {
                return balance(join(left, right->left, allocated), right->right,
                               allocated);
            }
            return branch(std::move(left), std::move(right), allocated);
        }
        // Coalesce the splice boundary, including boundaries hidden inside
        // balanced subtrees. Repeated cut/paste restoration must not leave a
        // growing directory of artificial fragments behind.
        static Tree concatenate(Tree left, Tree right, uint64_t &allocated)
        {
            if (!left || !right)
            {
                return join(std::move(left), std::move(right), allocated);
            }
            auto last = left, first = right;
            while (last->height > 1)
            {
                last = last->right;
            }
            while (first->height > 1)
            {
                first = first->left;
            }
            const auto a = last->range;
            const auto b = first->range;
            const bool contiguous =
                a.source == b.source &&
                (a.source
                     ? a.channel == b.channel && a.start + a.frames == b.start
                     : std::bit_cast<uint32_t>(a.constantValue) ==
                           std::bit_cast<uint32_t>(b.constantValue));
            if (!contiguous)
            {
                return join(std::move(left), std::move(right), allocated);
            }
            auto before = split(left, left->frames - a.frames, allocated).first;
            auto after = split(right, b.frames, allocated).second;
            auto merged = a;
            merged.frames = sum(a.frames, b.frames);
            return join(join(std::move(before),
                             leaf(std::move(merged), allocated), allocated),
                        std::move(after), allocated);
        }
        static std::pair<Tree, Tree> split(const Tree &tree, int64_t at,
                                           uint64_t &allocated)
        {
            if (!at)
            {
                return {{}, tree};
            }
            if (at == length(tree))
            {
                return {tree, {}};
            }
            if (tree->height == 1)
            {
                auto first = tree->range, second = first;
                first.frames = at;
                if (second.source)
                {
                    second.start += at;
                }
                second.frames -= at;
                return {leaf(std::move(first), allocated),
                        leaf(std::move(second), allocated)};
            }
            const auto leftFrames = length(tree->left);
            if (at < leftFrames)
            {
                auto [first, second] = split(tree->left, at, allocated);
                return {first, join(second, tree->right, allocated)};
            }
            auto [first, second] =
                split(tree->right, at - leftFrames, allocated);
            return {join(tree->left, first, allocated), second};
        }
        template <typename Visitor>
        static void visit(const Tree &tree, int64_t start, int64_t count,
                          Visitor &visitor)
        {
            if (!count)
            {
                return;
            }
            if (tree->height == 1)
            {
                auto range = tree->range;
                if (range.source)
                {
                    range.start += start;
                }
                range.frames = count;
                visitor(range);
                return;
            }
            const auto leftFrames = length(tree->left);
            if (start < leftFrames)
            {
                const auto take = std::min(count, leftFrames - start);
                visit(tree->left, start, take, visitor);
                count -= take;
                start += take;
            }
            if (count)
            {
                visit(tree->right, start - leftFrames, count, visitor);
            }
        }
        static bool prepare(const Tree &tree, PeakWork &work,
                            const std::function<bool()> &cancel)
        {
            if (!tree)
            {
                return true;
            }
            if (cancel && cancel())
            {
                return false;
            }
            ++work.visitedNodes;
            if (tree.prepared())
            {
                return true;
            }
            auto prepared = std::make_shared<PreparedPeaks>();
            if (tree->height > 1)
            {
                if (!prepare(tree->left, work, cancel) ||
                    !prepare(tree->right, work, cancel))
                {
                    return false;
                }
                prepared->whole =
                    waveform::combine(tree->left.prepared()->whole,
                                      tree->right.prepared()->whole);
            }
            else if (!tree->range.source)
            {
                prepared->whole = {tree->range.constantValue,
                                   tree->range.constantValue};
            }
            else
            {
                const auto range = tree->range;
                const auto &source = *range.source;
                if (!source.sourcePeaks())
                {
                    return false;
                }
                constexpr auto block = waveform::SourcePeaks::blockFrames;
                const auto end = range.start + range.frames;
                prepared->headFrames = std::min(
                    range.frames, (block - range.start % block) % block);
                prepared->tailFrames = prepared->headFrames == range.frames ||
                                               end == source.shape().frames
                                           ? 0
                                           : end % block;
                const auto readEdge = [&](int64_t start, int64_t frames)
                {
                    auto result = waveform::emptyPeak();
                    if (frames)
                    {
                        std::array<float, block> samples;
                        source.readChannel(
                            range.channel, start,
                            std::span<float>(samples).first(frames));
                        work.boundarySamples += frames;
                        for (int64_t i = 0; i < frames; ++i)
                        {
                            result = waveform::combine(
                                result, {samples[i], samples[i]});
                        }
                    }
                    return result;
                };
                prepared->head = readEdge(range.start, prepared->headFrames);
                if (cancel && cancel())
                {
                    return false;
                }
                prepared->tail =
                    readEdge(end - prepared->tailFrames, prepared->tailFrames);
                prepared->whole =
                    waveform::combine(prepared->head, prepared->tail);
                const auto first = range.start + prepared->headFrames;
                const auto last = end - prepared->tailFrames;
                if (last > first)
                {
                    prepared->whole = waveform::combine(
                        prepared->whole, source.sourcePeaks()->queryBlocks(
                                             range.channel, first / block,
                                             last / block + (last % block != 0),
                                             work.sourcePeaks));
                }
            }
            if (cancel && cancel())
            {
                return false;
            }
            std::shared_ptr<const PreparedPeaks> immutable =
                std::move(prepared);
            tree.setPrepared(*immutable);
            ++work.preparedNodes;
            return true;
        }
        static std::optional<waveform::Peak>
        overview(const Tree &tree, int64_t start, int64_t count, PeakWork &work)
        {
            if (!count)
            {
                return waveform::emptyPeak();
            }
            ++work.visitedNodes;
            const auto prepared = tree.prepared();
            if (start == 0 && count == tree->frames)
            {
                return prepared ? std::optional{prepared->whole} : std::nullopt;
            }
            if (tree->height == 1)
            {
                if (!prepared)
                {
                    return std::nullopt; // Pending never reads disk.
                }
                const auto range = tree->range;
                if (!range.source)
                {
                    return waveform::Peak{range.constantValue,
                                          range.constantValue};
                }
                const auto end = start + count;
                auto result = waveform::emptyPeak();
                if (start < prepared->headFrames)
                {
                    result = waveform::combine(result, prepared->head);
                }
                if (end > tree->frames - prepared->tailFrames)
                {
                    result = waveform::combine(result, prepared->tail);
                }
                const auto first =
                    range.start + std::max(start, prepared->headFrames);
                const auto last =
                    range.start +
                    std::min(end, tree->frames - prepared->tailFrames);
                if (last > first)
                {
                    constexpr auto block = waveform::SourcePeaks::blockFrames;
                    result = waveform::combine(
                        result, range.source->sourcePeaks()->queryBlocks(
                                    range.channel, first / block,
                                    last / block + (last % block != 0),
                                    work.sourcePeaks));
                }
                return result;
            }
            auto result = waveform::emptyPeak();
            const auto leftFrames = length(tree->left);
            if (start < leftFrames)
            {
                const auto take = std::min(count, leftFrames - start);
                auto peak = overview(tree->left, start, take, work);
                if (!peak)
                {
                    return std::nullopt;
                }
                result = *peak;
                count -= take;
                start += take;
            }
            if (count)
            {
                auto peak =
                    overview(tree->right, start - leftFrames, count, work);
                if (!peak)
                {
                    return std::nullopt;
                }
                result = waveform::combine(result, *peak);
            }
            return result;
        }
        static void validateFrames(AudioShape shape, int64_t start,
                                   int64_t count)
        {
            if (count < 0)
            {
                throw std::out_of_range("Negative audio range");
            }
            validateRange(shape, 0, start, std::size_t(count));
        }
        static void validateShape(AudioShape shape)
        {
            const bool unconfigured = shape.frames == 0 &&
                                      shape.channels == 0 &&
                                      shape.sampleRate >= 0;
            if (shape.channels > 256 || shape.format < SampleFormat::PCM_S8 ||
                shape.format > SampleFormat::Unknown ||
                (!unconfigured && (shape.frames < 0 || shape.channels <= 0 ||
                                   shape.sampleRate <= 0 ||
                                   shape.format == SampleFormat::Unknown)))
            {
                throw std::invalid_argument("Invalid audio revision shape");
            }
        }
        AudioEditRevision(AudioShape shape, std::vector<Tree> roots)
            : dimensions(shape),
              rootMemory(reserveWorking(sizeof(AudioEditRevision) +
                                            roots.capacity() * sizeof(Tree),
                                        MemoryUse::Index)),
              channels(std::move(roots))
        {
            for (auto &root : channels)
            {
                root = root.pin();
            }
            if (!channels.empty())
            {
                originalSource = channels.front().ownedSource();
            }
        }

    public:
        static std::shared_ptr<const AudioEditRevision>
        silence(AudioShape shape)
        {
            validateShape(shape);
            std::vector<Tree> roots;
            uint64_t allocated = 0;
            for (int c = 0; c < shape.channels; ++c)
            {
                roots.push_back(leaf({{}, c, 0, shape.frames}, allocated));
            }
            return std::shared_ptr<const AudioEditRevision>(
                new AudioEditRevision(shape, std::move(roots)));
        }
        static std::shared_ptr<const AudioEditRevision>
        from(std::shared_ptr<const AudioRevision> source)
        {
            if (!source)
            {
                throw std::invalid_argument("Missing source revision");
            }
            const auto shape = source->shape();
            std::vector<Tree> roots;
            uint64_t allocated = 0;
            for (int c = 0; c < shape.channels; ++c)
            {
                roots.push_back(leaf({source, c, 0, shape.frames}, allocated));
            }
            return std::shared_ptr<const AudioEditRevision>(
                new AudioEditRevision(shape, std::move(roots)));
        }
        std::shared_ptr<const AudioEditRevision>
        forPaste(AudioShape target) const
        {
            if (target.channels <= 0 || target.sampleRate <= 0)
            {
                throw std::invalid_argument("Invalid paste target");
            }
            target.frames = dimensions.frames;
            auto roots = channels;
            roots.resize(target.channels);
            uint64_t allocated = 0;
            for (int c = dimensions.channels; c < target.channels; ++c)
            {
                roots[c] = leaf({{}, c, 0, target.frames}, allocated);
            }
            return std::shared_ptr<const AudioEditRevision>(
                new AudioEditRevision(target, std::move(roots)));
        }
        // Legacy recording changes shape without resampling retained frames.
        // Reuse common channels, trim excess frames and synthesize new silence.
        std::shared_ptr<const AudioEditRevision>
        withShape(AudioShape target) const
        {
            validateShape(target);
            std::vector<Tree> roots(target.channels);
            uint64_t allocated = 0;
            for (int c = 0; c < target.channels; ++c)
            {
                auto root = c < dimensions.channels ? channels[c] : Tree{};
                if (length(root) > target.frames)
                {
                    root = split(root, target.frames, allocated).first;
                }
                if (length(root) < target.frames)
                {
                    root = concatenate(
                        root,
                        leaf({{}, c, 0, target.frames - length(root)},
                             allocated),
                        allocated);
                }
                roots[c] = std::move(root);
            }
            return std::shared_ptr<const AudioEditRevision>(
                new AudioEditRevision(target, std::move(roots)));
        }
        // Worker-only metadata lookup; provenance pages may read disk.
        bool isDirty(int channel, int64_t frame) const
        {
            bool dirty = true;
            visitSourceRanges(channel, frame, 1,
                              [&](const SourceRange &range)
                              {
                                  if (range.source)
                                  {
                                      audio::SampleProvenance provenance;
                                      uint8_t flag = 1;
                                      range.source->readLegacyMetadata(
                                          range.channel, range.start,
                                          {&provenance, 1}, {&flag, 1});
                                      dirty = flag != 0;
                                  }
                              });
            return dirty;
        }
        std::shared_ptr<const AudioRevision> ownedSource() const
        {
            return originalSource;
        }
        AudioShape shape() const override
        {
            return dimensions;
        }
        void readDirtyFlags(int channel, int64_t start,
                            std::span<uint8_t> output) const override
        {
            visitSourceRanges(channel, start, int64_t(output.size()),
                              [&](const SourceRange &range)
                              {
                                  auto part =
                                      output.first(std::size_t(range.frames));
                                  if (range.source)
                                  {
                                      range.source->readDirtyFlags(
                                          range.channel, range.start, part);
                                  }
                                  else
                                  {
                                      std::fill(part.begin(), part.end(), 1);
                                  }
                                  output = output.subspan(part.size());
                              });
        }
        template <typename Visitor>
        void visitSourceRanges(int channel, int64_t start, int64_t count,
                               Visitor visitor) const
        {
            if (count < 0)
            {
                throw std::out_of_range("Negative audio range");
            }
            validateRange(dimensions, channel, start, std::size_t(count));
            visit(channels[channel], start, count, visitor);
        }
        void readChannel(int channel, int64_t start,
                         std::span<float> output) const override
        {
            validateRange(dimensions, channel, start, output.size());
            visitSourceRanges(
                channel, start, int64_t(output.size()),
                [&](const SourceRange &range)
                {
                    const auto destination =
                        output.first(std::size_t(range.frames));
                    if (range.source)
                    {
                        range.source->readChannel(range.channel, range.start,
                                                  destination);
                    }
                    else
                    {
                        std::fill(destination.begin(), destination.end(),
                                  range.constantValue);
                    }
                    output = output.subspan(destination.size());
                });
        }
        // Worker-only: exact summaries for cut boundaries require at most
        // 254 samples per new leaf. Previously prepared subtrees are reused.
        bool prepareWaveform(PeakWork &work,
                             const std::function<bool()> &cancel = {}) const
        {
            for (const auto &root : channels)
            {
                if (!prepare(root, work, cancel))
                {
                    return false;
                }
            }
            return true;
        }
        // Worker-only overview query; detailed source peaks may read disk.
        // Pixel edges may expand within a leaf to
        // a 128-frame source bucket; never beyond an edit boundary. Whole-tree
        // and whole-leaf summaries are exact. Fine zoom uses async sample
        // reads.
        std::optional<waveform::Peak>
        queryWaveformOverview(int channel, int64_t start, int64_t count,
                              PeakWork &work) const
        {
            if (count <= 0)
            {
                throw std::out_of_range("Empty waveform range");
            }
            validateRange(dimensions, channel, start, std::size_t(count));
            return overview(channels[channel], start, count, work);
        }
        EditTree::Stats indexStats() const
        {
            for (const auto &root : channels)
            {
                if (root)
                {
                    return root.stats();
                }
            }
            return {};
        }
        int indexHeight() const
        {
            int result = 0;
            for (const auto &root : channels)
            {
                result = std::max(result, height(root));
            }
            return result;
        }
    };

    // A transaction owns a candidate root set. Each operation constructs all
    // replacement roots before changing the candidate; failure/cancellation
    // leaves the caller's committed revision intact. No sample I/O occurs here.
    class AudioEditTransaction
    {
        using Revision = AudioEditRevision;
        using Tree = Revision::Tree;
        AudioShape dimensions;
        std::vector<Tree> channels;
        uint64_t allocated = 0;

        static void compatible(AudioShape a, AudioShape b)
        {
            if (a.channels != b.channels || a.sampleRate != b.sampleRate ||
                a.format != b.format)
            {
                throw std::invalid_argument(
                    "Audio edit requires compatible formats");
            }
        }
        void splice(int64_t at, int64_t remove, const Revision *insert)
        {
            Revision::validateFrames(dimensions, at, remove);
            if (insert)
            {
                compatible(dimensions, insert->shape());
            }
            const auto inserted = insert ? insert->shape().frames : 0;
            const auto frames =
                Revision::sum(dimensions.frames - remove, inserted);
            if (!remove && !inserted)
            {
                return;
            }
            auto next = channels;
            for (int c = 0; c < dimensions.channels; ++c)
            {
                auto [left, tail] = Revision::split(channels[c], at, allocated);
                auto [discard, right] =
                    Revision::split(tail, remove, allocated);
                next[c] = Revision::concatenate(
                    Revision::concatenate(
                        left, insert ? insert->channels[c] : Tree{}, allocated),
                    right, allocated);
            }
            channels = std::move(next);
            dimensions.frames = frames;
        }

    public:
        explicit AudioEditTransaction(const Revision &base)
            : dimensions(base.dimensions), channels(base.channels)
        {
        }
        void erase(int64_t at, int64_t count)
        {
            splice(at, count, nullptr);
        }
        void insert(int64_t at, const Revision &source)
        {
            splice(at, 0, &source);
        }
        void replace(int64_t at, int64_t count, const Revision &source)
        {
            splice(at, count, &source);
        }
        void trim(int64_t start, int64_t count)
        {
            Revision::validateFrames(dimensions, start, count);
            auto next = channels;
            for (int c = 0; c < dimensions.channels; ++c)
            {
                auto [discard, tail] =
                    Revision::split(channels[c], start, allocated);
                next[c] = Revision::split(tail, count, allocated).first;
            }
            channels = std::move(next);
            dimensions.frames = count;
        }
        // A channel-specific replacement preserves the document's duration.
        // A null source means silence; mapping/provenance follows
        // sourceChannel.
        void replaceChannel(int channel, int64_t at, int64_t count,
                            const Revision *source = nullptr,
                            int sourceChannel = 0, int64_t sourceStart = 0,
                            float constantValue = 0)
        {
            Revision::validateFrames(dimensions, at, count);
            AudioReader::validateRange(dimensions, channel, at,
                                       std::size_t(count));
            if (source)
            {
                if (source->shape().sampleRate != dimensions.sampleRate ||
                    source->shape().format != dimensions.format)
                {
                    throw std::invalid_argument(
                        "Audio edit requires compatible formats");
                }
                AudioReader::validateRange(source->shape(), sourceChannel,
                                           sourceStart, std::size_t(count));
            }
            if (!count)
            {
                return;
            }
            Tree replacement;
            if (source)
            {
                auto tail = Revision::split(source->channels[sourceChannel],
                                            sourceStart, allocated)
                                .second;
                replacement = Revision::split(tail, count, allocated).first;
            }
            else
            {
                replacement =
                    Revision::leaf({{}, 0, 0, count, constantValue}, allocated);
            }
            auto [left, tail] =
                Revision::split(channels[channel], at, allocated);
            auto right = Revision::split(tail, count, allocated).second;
            auto next = Revision::concatenate(
                Revision::concatenate(left, replacement, allocated), right,
                allocated);
            channels[channel] = std::move(next);
        }
        std::shared_ptr<const Revision> finish() const
        {
            return std::shared_ptr<const Revision>(
                new Revision(dimensions, channels));
        }
        uint64_t allocatedIndexNodes() const
        {
            return allocated;
        }
    };
} // namespace cupuacu::storage
