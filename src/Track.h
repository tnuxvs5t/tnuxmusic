#pragma once

#include <QJsonObject>
#include <QString>
#include <QVariantMap>
#include <QVector>

struct TrackQuality {
    QString label;
    QString path;
    QString codec;
    int bitrate = 0;
    int sampleRate = 0;

    QJsonObject toJson() const;
    static TrackQuality fromJson(const QJsonObject &obj);
    QVariantMap toVariantMap() const;
};

struct Track {
    QString id;
    QString title;
    QString artist;
    QString album;
    QString genre;
    QString coverPath;
    QString lyricPath;
    int year = 0;
    int disc = 1;
    int trackNo = 0;
    QVector<TrackQuality> qualities;

    QString primaryPath() const;
    QString displayTitle() const;
    QString normalizedKey() const;
    QString qualitiesText() const;
    QJsonObject toJson() const;
    QVariantMap toVariantMap() const;
    static Track fromJson(const QJsonObject &obj);
};

QString canonicalLocalPath(const QString &pathOrUrl);
QString fileUrlFromPath(const QString &path);
QString stableTrackId(const Track &track);

