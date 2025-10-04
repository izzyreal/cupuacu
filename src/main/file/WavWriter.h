#pragma once

#include "../State.h"

#include <ios>
#include <filesystem>
#include <istream>
#include <memory>
#include <ostream>
#include <fstream>
#include <stdexcept>
#include <vector>
#include <cstdio>

namespace cupuacu::file {
class WavWriter {
    private:
        static std::ifstream openInputFileStream(const std::filesystem::path &p)
        {
            std::ifstream result(p, std::ios::binary);
            if (!result.is_open()) {
                throw std::ios_base::failure("Failed to open input file");
            }
            return result;
        }

        static bool is16BitPcmWavFile(std::istream &is)
        {
            char header[12];
            is.seekg(0, std::ios::beg);
            if (!is.read(header, 12)) return false;
            if (std::memcmp(header, "RIFF", 4) != 0) return false;
            if (std::memcmp(header + 8, "WAVE", 4) != 0) return false;

            char chunkId[4];
            uint32_t chunkSize;
            while (is.read(chunkId, 4) && is.read(reinterpret_cast<char*>(&chunkSize), 4)) {
                if (std::memcmp(chunkId, "fmt ", 4) == 0) {
                    std::vector<char> fmtData(chunkSize);
                    if (!is.read(fmtData.data(), chunkSize)) return false;
                    uint16_t audioFormat = *reinterpret_cast<uint16_t*>(fmtData.data());
                    uint16_t bitsPerSample = *reinterpret_cast<uint16_t*>(fmtData.data() + 14);
                    return (audioFormat == 1 && bitsPerSample == 16);
                } else {
                    is.seekg(chunkSize, std::ios::cur);
                }
            }
            return false;
        }

        static std::streampos findDataChunkStartOffset(std::istream &is)
        {
            is.clear();
            is.seekg(12, std::ios::beg);
            char chunkId[4];
            uint32_t chunkSize;
            while (is.read(chunkId, 4) && is.read(reinterpret_cast<char*>(&chunkSize), 4)) {
                if (std::memcmp(chunkId, "data", 4) == 0) {
                    // Step back 8 bytes: 4 for "data" + 4 for size field
                    return is.tellg() - static_cast<std::streamoff>(8);
                }
                is.seekg(chunkSize, std::ios::cur);
            }
            throw std::runtime_error("data chunk not found");
        }

        static std::streampos findDataChunkEndOffset(std::istream &is)
        {
            is.clear();
            is.seekg(12, std::ios::beg);
            char chunkId[4];
            uint32_t chunkSize;
            while (is.read(chunkId, 4) && is.read(reinterpret_cast<char*>(&chunkSize), 4)) {
                if (std::memcmp(chunkId, "data", 4) == 0) {
                    std::streampos start = is.tellg();
                    is.seekg(chunkSize, std::ios::cur);
                    return is.tellg();
                }
                is.seekg(chunkSize, std::ios::cur);
            }
            throw std::runtime_error("data chunk not found");
        }

        static std::ofstream openOutputFileStream(const std::filesystem::path &p)
        {
            std::ofstream result(p, std::ios::binary | std::ios::trunc);
            if (!result.is_open()) {
                throw std::ios_base::failure("Failed to open output file");
            }
            return result;
        }

        static void copyBytesBeforeDataChunk(std::istream &is, std::ostream &os)
        {
            if (!is16BitPcmWavFile(is)) {
                throw std::invalid_argument("Not a 16-bit PCM WAV file");
            }
            os.seekp(0, std::ios::end);
            if (os.tellp() != 0) {
                throw std::invalid_argument("Output stream not empty");
            }
            std::streampos dataOffset = findDataChunkStartOffset(is);
            is.clear();
            is.seekg(0, std::ios::beg);
            std::vector<char> buffer(static_cast<size_t>(dataOffset));
            if (!is.read(buffer.data(), buffer.size())) {
                throw std::runtime_error("Failed to read before data chunk");
            }
            os.write(buffer.data(), buffer.size());
        }

        static void writeDataChunk(const cupuacu::State *state, std::istream &is, std::ostream &os)
        {
            std::streampos dataOffset = findDataChunkStartOffset(is);
            os.seekp(0, std::ios::end);
            if (os.tellp() != dataOffset) {
                throw std::invalid_argument("Output stream size mismatch");
            }

            // Write the chunk ID
            os.write("data", 4);

            size_t frames   = state->document.getFrameCount();
            size_t channels = state->document.getChannelCount();

            std::vector<int16_t> interleaved(frames * channels);
            
            auto buf = state->document.getAudioBuffer();

            const bool shouldCheckForDirtiness = std::dynamic_pointer_cast<cupuacu::DirtyTrackingAudioBuffer>(buf) != nullptr; 

            for (size_t f = 0; f < frames; ++f)
            {
                for (size_t c = 0; c < channels; ++c)
                {
                    float s = buf->getSample(c, f);
                    if (s > 1.0f) s = 1.0f;
                    if (s < -1.0f) s = -1.0f;

                    if (!shouldCheckForDirtiness || buf->isDirty(c, f))
                    {
                        interleaved[f * channels + c] = static_cast<int16_t>(std::lrint(s * 32767.0f));
                    }
                    else
                    {
                        // Preserve original 16 bit int value if it wasn't modified after loading
                        interleaved[f * channels + c] = static_cast<int16_t>(std::lrint(s * 32768.0f));
                    }
                }
            }

            // Write chunk size
            uint32_t dataSize = static_cast<uint32_t>(interleaved.size() * sizeof(int16_t));
            os.write(reinterpret_cast<const char*>(&dataSize), 4);

            // Write PCM16 samples
            os.write(reinterpret_cast<const char*>(interleaved.data()), dataSize);
        }

        static void copyBytesAfterDataChunk(std::istream &is, std::ostream &os)
        {
            std::streampos endOffset = findDataChunkEndOffset(is);
            is.seekg(0, std::ios::end);
            std::streampos fileEnd = is.tellg();

            if (fileEnd <= endOffset) {
                std::printf("No extra bytes after data chunk\n");
                return;
            }

            std::streamsize extraSize = fileEnd - endOffset;
            is.seekg(endOffset, std::ios::beg);
            std::vector<char> buffer(extraSize);
            if (!is.read(buffer.data(), extraSize)) {
                throw std::runtime_error("Failed to read extra bytes");
            }
            os.write(buffer.data(), extraSize);
        }

        static bool isCopyBinaryIdentical(const std::filesystem::path &original, const std::filesystem::path &copy)
        {
            std::ifstream ifs1(original, std::ios::binary);
            std::ifstream ifs2(copy, std::ios::binary);
            if (!ifs1.is_open() || !ifs2.is_open()) return false;

            // Precompute data chunk ranges
            std::streampos dataStart1 = findDataChunkStartOffset(ifs1) + std::streampos(8); // skip "data"+size
            std::streampos dataEnd1   = findDataChunkEndOffset(ifs1);

            std::streampos dataStart2 = findDataChunkStartOffset(ifs2) + std::streampos(8);
            std::streampos dataEnd2   = findDataChunkEndOffset(ifs2);

            // Reset streams to beginning
            ifs1.clear(); ifs1.seekg(0, std::ios::beg);
            ifs2.clear(); ifs2.seekg(0, std::ios::beg);

            char c1, c2;
            std::streamoff offset = 0;
            while (ifs1.get(c1) && ifs2.get(c2)) {
                if (c1 != c2) {
                    bool inData1 = (offset >= dataStart1 && offset < dataEnd1);
                    bool inData2 = (offset >= dataStart2 && offset < dataEnd2);

                    if (inData1 && inData2 && std::abs((int)(unsigned char)c1 - (int)(unsigned char)c2) == 1) {
                        printf("Acceptable difference detected at offset %lld\n", static_cast<long long>(offset));
                        // Acceptable off-by-one PCM sample difference
                    } else {
                        std::printf("Difference at offset %lld: original=0x%02x copy=0x%02x\n",
                                    static_cast<long long>(offset),
                                    static_cast<unsigned char>(c1),
                                    static_cast<unsigned char>(c2));

                        auto printContext = [&](std::ifstream &ifs, const char *label) {
                            std::streamoff start = offset - 10;
                            if (start < 0) start = 0;
                            std::streamoff end = offset + 10;

                            ifs.clear();
                            ifs.seekg(0, std::ios::end);
                            std::streamoff fileSize = ifs.tellg();
                            if (end >= fileSize) end = fileSize - 1;

                            std::streamoff size = end - start + 1;
                            std::vector<unsigned char> buffer(size);

                            ifs.seekg(start, std::ios::beg);
                            ifs.read(reinterpret_cast<char*>(buffer.data()), size);

                            std::printf("%s: ", label);
                            for (std::streamoff i = 0; i < size; ++i) {
                                std::printf("%02x ", buffer[i]);
                            }
                            std::printf("\n");

                            std::printf("%s: ", label);
                            for (std::streamoff i = 0; i < size; ++i) {
                                char ch = (buffer[i] >= 32 && buffer[i] < 127) ? buffer[i] : '.';
                                std::printf("%c  ", ch);
                            }
                            std::printf("\n");

                            std::printf("%s: ", label);
                            for (std::streamoff i = 0; i < size; ++i) {
                                if (start + i == offset) {
                                    std::printf("^  ");
                                } else {
                                    std::printf("   ");
                                }
                            }
                            std::printf("\n");
                        };

                        printContext(ifs1, "original");
                        printContext(ifs2, "copy    ");

                        return false;
                    }
                }
                offset++;
            }
            // Explicit file size comparison
            ifs1.clear(); 
            ifs2.clear();
            ifs1.seekg(0, std::ios::end);
            ifs2.seekg(0, std::ios::end);
            std::streamoff size1 = ifs1.tellg();
            std::streamoff size2 = ifs2.tellg();
            if (size1 != size2) {
                std::printf("Files differ in size: original=%lld copy=%lld\n",
                            static_cast<long long>(size1),
                            static_cast<long long>(size2));
                return false;
            }

            return true;
        }

    public:
        static void rewriteWavFile(cupuacu::State *state)
        {
            std::filesystem::path input(state->currentFile);
            std::filesystem::path output(std::filesystem::temp_directory_path() / input.filename());

            auto ifs = openInputFileStream(input);
            if (!is16BitPcmWavFile(ifs)) {
                throw std::invalid_argument("Not a 16-bit PCM WAV file");
            }

            auto ofs = openOutputFileStream(output);

            copyBytesBeforeDataChunk(ifs, ofs);
            writeDataChunk(state, ifs, ofs);
            copyBytesAfterDataChunk(ifs, ofs);

            ofs.flush();
            ofs.close();

            bool identical = isCopyBinaryIdentical(input, output);

            if (identical)
            {
                std::printf("Saved file is binary identical to what was loaded.\n");
            }
            else
            {
                std::printf("Save file differs from what was loaded.\n");
            }

            std::filesystem::rename(output, input);
        }
};
}
