#pragma once

#include <QString>

struct NcmImportResult {
    enum class Status {
        Unsupported,
        Converted,
        Failed
    };

    Status status = Status::Unsupported;
    QString outputAudioPath;
    QString message;
};

class NcmImportService {
public:
    // Extension point for .ncm support.
    //
    // Contract:
    // - inputPath is an existing .ncm file discovered by the library scanner.
    // - Implementations may create a normal audio file beside the source file
    //   or in an application cache directory.
    // - On success, return Status::Converted and set outputAudioPath to the
    //   generated MP3/FLAC/M4A/etc. file. The scanner will then process that
    //   output file exactly like any other local audio file, including LRC->TLY.
    // - On unsupported default builds, keep Status::Unsupported.
    // - On recoverable failure, return Status::Failed with a user-facing message.
    //
    // This project intentionally leaves the actual NCM conversion body empty.
    // Fill this function in your own local branch if you want to provide the
    // converter implementation.
    static NcmImportResult convertToOpenAudio(const QString &inputPath);
};

