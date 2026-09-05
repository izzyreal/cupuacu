#pragma once

#include "AudioRevision.hpp"
#include <utility>

namespace cupuacu::storage
{
    // A persistent AVL sequence of ranges in immutable imported block
    // directories. Leaves never reference edited revisions, so history cannot
    // create recursive reader chains. Final release remains worker-only.
    class AudioEditRevision final : public AudioReader
    {
    public:
        struct SourceRange
        {
            std::shared_ptr<const AudioRevision> source; // null means silence
            int channel = 0;
            int64_t start = 0;
            int64_t frames = 0;
        };

    private:
        friend class AudioEditTransaction;
        struct Node;
        using Tree = std::shared_ptr<const Node>;
        struct Node
        {
            Tree left, right;
            SourceRange range;
            int64_t frames;
            int height;
        };
        AudioShape dimensions;
        std::vector<Tree> channels;

        static int height(const Tree &tree)
        {
            return tree ? tree->height : 0;
        }
        static int64_t length(const Tree &tree)
        {
            return tree ? tree->frames : 0;
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
            return std::make_shared<Node>(
                Node{{}, {}, std::move(range), frames, 1});
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
            return std::make_shared<Node>(
                Node{std::move(left), std::move(right), {}, frames, depth});
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
                const auto &middle = left->right;
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
                const auto &middle = right->left;
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
        static void validateFrames(AudioShape shape, int64_t start,
                                   int64_t count)
        {
            if (count < 0)
            {
                throw std::out_of_range("Negative audio range");
            }
            validateRange(shape, 0, start, std::size_t(count));
        }
        AudioEditRevision(AudioShape shape, std::vector<Tree> roots)
            : dimensions(shape), channels(std::move(roots))
        {
        }

    public:
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
        AudioShape shape() const override
        {
            return dimensions;
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
                        std::fill(destination.begin(), destination.end(), 0.0f);
                    }
                    output = output.subspan(destination.size());
                });
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
                next[c] = Revision::join(
                    Revision::join(left, insert ? insert->channels[c] : Tree{},
                                   allocated),
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
                            int sourceChannel = 0, int64_t sourceStart = 0)
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
                replacement = Revision::leaf({{}, 0, 0, count}, allocated);
            }
            auto [left, tail] =
                Revision::split(channels[channel], at, allocated);
            auto right = Revision::split(tail, count, allocated).second;
            auto next = Revision::join(
                Revision::join(left, replacement, allocated), right, allocated);
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
