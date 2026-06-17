#include "NcmImportService.h"

#include <QFileInfo>

NcmImportResult NcmImportService::convertToOpenAudio(const QString &inputPath)
{
    NcmImportResult result;
    result.status = NcmImportResult::Status::Unsupported;
    result.outputAudioPath.clear();
    result.message = QStringLiteral("NCM converter API is not implemented: %1")
                         .arg(QFileInfo(inputPath).fileName());
    return result;
}

