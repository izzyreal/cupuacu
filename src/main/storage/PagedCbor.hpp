#pragma once
#include "WorkingMemory.hpp"
#include <filesystem>
#include <nlohmann/json.hpp>
#include <array>
#include <bit>
#include <cmath>
#include <fstream>

namespace cupuacu::storage
{
    // Worker-only reader for Cupuacu's definite-length CBOR records. Large
    // legacy arrays retain byte extents in the immutable committed index.
    // Visitors decode one row at a time; the original archive remains intact.
    class PagedCbor
    {
        using Json = nlohmann::json;
        std::filesystem::path sourcePath;
        class Reader
        {
            std::istream &in;
            std::streambuf *inputBuffer;
            uint64_t remaining;
            const std::function<void()> &check;
            uint64_t residentEstimate = 0;
            static constexpr uint64_t limit = 2 * 1024 * 1024;
            std::shared_ptr<void> memory =
                reserveWorking(limit, MemoryUse::Index);
            void charge(uint64_t bytes)
            {
                if (bytes > limit - residentEstimate)
                {
                    throw std::runtime_error(
                        "Legacy metadata object is too complex");
                }
                residentEstimate += bytes;
            }
            uint8_t byte()
            {
                if (!remaining)
                {
                    throw std::runtime_error("Truncated legacy CBOR");
                }
                const int value = inputBuffer->sbumpc();
                if (value < 0)
                {
                    throw std::runtime_error("Truncated legacy CBOR");
                }
                --remaining;
                return uint8_t(value);
            }
            uint64_t argument(uint8_t info)
            {
                if (info < 24)
                {
                    return info;
                }
                if (info > 27)
                {
                    throw std::runtime_error(
                        "Unsupported legacy CBOR encoding");
                }
                uint64_t value = 0;
                for (unsigned i = 0; i < (1u << (info - 24)); ++i)
                {
                    value = (value << 8) | byte();
                }
                return value;
            }
            void skip(unsigned depth)
            {
                if (depth > 32)
                {
                    throw std::runtime_error("Legacy CBOR nesting is too deep");
                }
                const auto type = byte();
                const auto major = type >> 5;
                const auto count = argument(type & 31);
                if (major == 0 || major == 1 || major == 7)
                {
                    return;
                }
                if (major == 2 || major == 3)
                {
                    if (count > remaining ||
                        count > (major == 2 ? 65536u : 4096u))
                    {
                        throw std::runtime_error(
                            "Invalid legacy CBOR byte/string length");
                    }
                    in.ignore(count);
                    if (!in)
                    {
                        throw std::runtime_error("Truncated legacy CBOR");
                    }
                    remaining -= count;
                    return;
                }
                if (major == 4 || major == 5)
                {
                    if (count > remaining || (major == 5 && count > 64))
                    {
                        throw std::runtime_error(
                            "Invalid legacy CBOR container size");
                    }
                    const auto values = major == 5 ? count * 2 : count;
                    for (uint64_t i = 0; i < values; ++i)
                    {
                        if (i % 256 == 0)
                        {
                            check();
                        }
                        skip(depth + 1);
                    }
                    return;
                }
                throw std::runtime_error("Unsupported legacy CBOR value");
            }
            Json value(unsigned depth)
            {
                if (depth > 32)
                {
                    throw std::runtime_error("Legacy CBOR nesting is too deep");
                }
                charge(96);
                const auto type = byte();
                const auto major = type >> 5, info = type & 31;
                const auto count = argument(info);
                if (major == 0)
                {
                    return count;
                }
                if (major == 1)
                {
                    if (count > uint64_t(INT64_MAX))
                    {
                        throw std::runtime_error("Legacy integer overflow");
                    }
                    return -1 - int64_t(count);
                }
                if (major == 2 || major == 3)
                {
                    if (count > remaining ||
                        count > (major == 2 ? 65536u : 4096u))
                    {
                        throw std::runtime_error(
                            "Invalid legacy CBOR byte/string length");
                    }
                    charge(count);
                    if (major == 3)
                    {
                        std::string text(count, '\0');
                        for (auto &c : text)
                        {
                            c = char(byte());
                        }
                        return text;
                    }
                    std::vector<uint8_t> bytes(count);
                    for (auto &b : bytes)
                    {
                        b = byte();
                    }
                    return Json::binary(std::move(bytes));
                }
                if (major == 4)
                {
                    if (count > remaining)
                    {
                        throw std::runtime_error(
                            "Invalid legacy CBOR array length");
                    }
                    if (count > 256)
                    {
                        const auto offset = in.tellg();
                        if (offset < 0)
                        {
                            throw std::runtime_error(
                                "Invalid legacy array position");
                        }
                        const auto before = remaining;
                        for (uint64_t i = 0; i < count; ++i)
                        {
                            if (i % 256 == 0)
                            {
                                check();
                            }
                            skip(depth + 1);
                        }
                        return {{"$offset", uint64_t(offset)},
                                {"count", count},
                                {"$bytes", before - remaining}};
                    }
                    Json values = Json::array();
                    for (uint64_t i = 0; i < count; ++i)
                    {
                        values.push_back(value(depth + 1));
                    }
                    return values;
                }
                if (major == 5)
                {
                    if (count > 64)
                    {
                        throw std::runtime_error(
                            "Invalid legacy CBOR object size");
                    }
                    Json object = Json::object();
                    for (uint64_t i = 0; i < count; ++i)
                    {
                        auto key = value(depth + 1);
                        if (!key.is_string())
                        {
                            throw std::runtime_error("Invalid legacy CBOR key");
                        }
                        auto name = key.get<std::string>();
                        if (object.contains(name))
                        {
                            throw std::runtime_error(
                                "Duplicate legacy CBOR key");
                        }
                        object[name] = value(depth + 1);
                    }
                    return object;
                }
                if (major == 7)
                {
                    if (info == 20)
                    {
                        return false;
                    }
                    if (info == 21)
                    {
                        return true;
                    }
                    if (info == 22)
                    {
                        return nullptr;
                    }
                    if (info == 26 || info == 27)
                    {
                        const double number =
                            info == 26
                                ? double(std::bit_cast<float>(uint32_t(count)))
                                : std::bit_cast<double>(count);
                        if (!std::isfinite(number))
                        {
                            throw std::runtime_error(
                                "Nonfinite legacy CBOR number");
                        }
                        return number;
                    }
                    if (info == 25)
                    {
                        const auto exponent = (count >> 10) & 31,
                                   fraction = count & 1023;
                        if (exponent == 31)
                        {
                            throw std::runtime_error(
                                "Nonfinite legacy CBOR number");
                        }
                        const auto magnitude =
                            exponent ? std::ldexp(1024. + fraction,
                                                  int(exponent) - 25)
                                     : std::ldexp(double(fraction), -24);
                        return count & 32768 ? -magnitude : magnitude;
                    }
                }
                throw std::runtime_error("Unsupported legacy CBOR value");
            }

        public:
            Reader(std::istream &in, uint64_t size,
                   const std::function<void()> &check)
                : in(in), inputBuffer(in.rdbuf()), remaining(size), check(check)
            {
            }
            void visit(uint64_t count,
                       const std::function<void(const Json &)> &visitor)
            {
                for (uint64_t i = 0; i < count; ++i)
                {
                    if (i % 256 == 0)
                    {
                        check();
                    }
                    visitor(value(0));
                    residentEstimate = 0;
                }
                if (remaining)
                {
                    throw std::runtime_error("Trailing legacy array bytes");
                }
            }
            Json read()
            {
                auto result = value(0);
                if (remaining)
                {
                    throw std::runtime_error("Trailing legacy CBOR bytes");
                }
                return result;
            }
        };

    public:
        Json read(const std::filesystem::path &path, uint64_t offset,
                  uint64_t bytes, const std::function<void()> &check)
        {
            sourcePath = path;
            std::ifstream in(path, std::ios::binary);
            in.seekg(offset);
            return Reader(in, bytes, check).read();
        }
        void visit(const Json &sequence,
                   const std::function<void(const Json &)> &visitor,
                   const std::function<void()> &check)
        {
            const auto offset = sequence.at("$offset").get<uint64_t>();
            const auto bytes = sequence.at("$bytes").get<uint64_t>();
            const auto count = sequence.at("count").get<uint64_t>();
            std::ifstream in(sourcePath, std::ios::binary);
            const auto length = std::filesystem::file_size(sourcePath);
            if (offset > length || bytes > length - offset)
            {
                throw std::runtime_error("Truncated legacy metadata array");
            }
            in.seekg(offset);
            Reader(in, bytes, check).visit(count, visitor);
        }
    };
} // namespace cupuacu::storage
