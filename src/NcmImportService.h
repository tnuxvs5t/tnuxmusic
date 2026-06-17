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
    // Contract:
    // - inputPath is an existing .ncm file discovered by the library scanner.
    // - Creates a normal audio file beside the source file.
    // - On success, return Status::Converted and set outputAudioPath to the
    //   generated MP3/FLAC/M4A/etc. file. The scanner will then process that
    //   output file exactly like any other local audio file, including LRC->TLY.
    // - On recoverable failure, return Status::Failed with a user-facing message.
    static NcmImportResult convertToOpenAudio(const QString &inputPath);
};
