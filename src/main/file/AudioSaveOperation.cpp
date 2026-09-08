#include "AudioSaveOperation.hpp"
#include "AudioFileWriter.hpp"
#include "OwnedSourceFile.hpp"
#include "PreservationBackend.hpp"

namespace cupuacu::file
{
    std::shared_ptr<const storage::AudioRevision> writeAudioSave(
        const AudioSaveSnapshot &input, const AudioSaveRequest &request,
        const WriteProgressCallback &progress)
    {
        if (request.preserving && request.referencePath.empty())
        {
            throw std::invalid_argument("Preserving save requires a reference file");
        }
        auto write = [&](const std::filesystem::path &output)
        {
            if (input.revision)
            {
                if (request.preserving)
                {
                    writePreservingRevision(
                        *input.revision, input.document.getMarkers(),
                        request.referencePath, output, request.settings, progress);
                }
                else
                {
                    AudioFileWriter::writeFile(
                        *input.revision, input.document.getMarkers(), output,
                        request.settings, progress);
                }
            }
            else
            {
                const auto lease = input.document.acquireReadLease();
                if (request.preserving)
                {
                    writePreservingFile(PreservationWriteInput{
                        lease, request.referencePath, output, request.settings, progress});
                }
                else
                {
                    AudioFileWriter::writeFile(lease, output, request.settings, progress);
                }
            }
        };
        if (!input.revision)
        {
            write(request.outputPath);
            return {};
        }
        return writeOwnedRevisionContainer(
            request.outputPath, request.workingRoot, input.revision->shape(), write,
            [&](double value)
            {
                if (progress)
                {
                    progress("Retaining saved source", value);
                }
            });
    }
}
