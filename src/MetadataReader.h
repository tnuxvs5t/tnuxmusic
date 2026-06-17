#pragma once

#include <QString>
#include <QByteArray>

struct AudioMetadata {
    QString title;
    QString artist;
    QString album;
    QString genre;
    QString coverMimeType;
    QByteArray coverData;
    int year = 0;
    int trackNo = 0;
    int disc = 0;

    bool hasAny() const;
};

class MetadataReader {
public:
    static AudioMetadata read(const QString &path);
};
