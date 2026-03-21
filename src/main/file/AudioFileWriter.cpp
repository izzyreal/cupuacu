#include "AudioFileWriter.hpp"

#include <sndfile.h>

#include <filesystem>
#include <ios>
#include <stdexcept>
#include <vector>

void cupuacu::file::AudioFileWriter::writeFile(
    cupuacu::State *state, const std::filesystem::path &outputPath,
    const AudioExportSettings &settings)
{
    if (!state || outputPath.empty())
    {
        throw std::invalid_argument("Output path is empty");
    }
    if (!settings.isValid())
    {
        throw std::invalid_argument("Export settings are invalid");
    }

    const auto &session = state->getActiveDocumentSession();
    const auto &document = session.document;
    const int channels = document.getChannelCount();
    const int sampleRate = document.getSampleRate();

    if (channels <= 0 || sampleRate <= 0)
    {
        throw std::invalid_argument("Document format cannot be exported");
    }

    std::error_code ec;
    const auto parentPath = outputPath.parent_path();
    if (!parentPath.empty())
    {
        std::filesystem::create_directories(parentPath, ec);
        if (ec)
        {
            throw std::ios_base::failure("Failed to create output directory");
        }
    }

    SF_INFO sfinfo{};
    sfinfo.channels = channels;
    sfinfo.samplerate = sampleRate;
    sfinfo.format = settings.sndfileFormat();
    if (sf_format_check(&sfinfo) == 0)
    {
        throw std::invalid_argument("Export format is not supported by libsndfile");
    }

    SNDFILE *snd = sf_open(outputPath.string().c_str(), SFM_WRITE, &sfinfo);
    if (!snd)
    {
        throw std::ios_base::failure("Failed to open output audio file");
    }

    const sf_count_t frames = document.getFrameCount();
    std::vector<float> interleaved(static_cast<std::size_t>(frames) *
                                   static_cast<std::size_t>(channels));
    for (sf_count_t frame = 0; frame < frames; ++frame)
    {
        for (int channel = 0; channel < channels; ++channel)
        {
            interleaved[static_cast<std::size_t>(frame) *
                            static_cast<std::size_t>(channels) +
                        static_cast<std::size_t>(channel)] =
                document.getSample(channel, frame);
        }
    }

    const sf_count_t written = sf_writef_float(snd, interleaved.data(), frames);
    sf_write_sync(snd);
    sf_close(snd);

    if (written != frames)
    {
        throw std::ios_base::failure("Failed to write all audio frames");
    }
}
