#include "Track.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonValue>
#include <QUrl>

static QString trimmedLower(QString s)
{
    return s.simplified().toCaseFolded();
}

QString canonicalLocalPath(const QString &pathOrUrl)
{
    QString s = pathOrUrl.trimmed();
    if (s.isEmpty())
        return {};

    const QUrl url(s);
    if (url.isValid() && url.isLocalFile())
        s = url.toLocalFile();

    QFileInfo info(s);
    if (info.exists())
        return info.canonicalFilePath();

    if (info.isAbsolute())
        return QDir::cleanPath(info.absoluteFilePath());

    return QDir::cleanPath(s);
}

QString fileUrlFromPath(const QString &path)
{
    if (path.trimmed().isEmpty())
        return {};
    return QUrl::fromLocalFile(canonicalLocalPath(path)).toString();
}

QJsonObject TrackQuality::toJson() const
{
    QJsonObject obj;
    obj["label"] = label;
    obj["path"] = path;
    obj["codec"] = codec;
    if (bitrate > 0)
        obj["bitrate"] = bitrate;
    if (sampleRate > 0)
        obj["sampleRate"] = sampleRate;
    return obj;
}

TrackQuality TrackQuality::fromJson(const QJsonObject &obj)
{
    TrackQuality q;
    q.label = obj.value("label").toString();
    q.path = canonicalLocalPath(obj.value("path").toString());
    q.codec = obj.value("codec").toString();
    q.bitrate = obj.value("bitrate").toInt();
    q.sampleRate = obj.value("sampleRate").toInt();
    return q;
}

QVariantMap TrackQuality::toVariantMap() const
{
    return {
        {"label", label},
        {"path", path},
        {"url", fileUrlFromPath(path)},
        {"codec", codec},
        {"bitrate", bitrate},
        {"sampleRate", sampleRate},
    };
}

QString Track::primaryPath() const
{
    if (qualities.isEmpty())
        return {};
    return qualities.first().path;
}

QString Track::displayTitle() const
{
    if (!title.trimmed().isEmpty())
        return title;
    const QString path = primaryPath();
    return path.isEmpty() ? QStringLiteral("Untitled") : QFileInfo(path).completeBaseName();
}

QString Track::normalizedKey() const
{
    QString a = trimmedLower(artist);
    QString b = trimmedLower(album);
    QString c = trimmedLower(displayTitle());
    if (a.isEmpty() && b.isEmpty())
        return QStringLiteral("path:%1").arg(trimmedLower(primaryPath()));
    return QStringLiteral("%1\001%2\001%3").arg(a, b, c);
}

QString Track::qualitiesText() const
{
    QStringList parts;
    parts.reserve(qualities.size());
    for (const auto &q : qualities) {
        QString label = q.label.trimmed().isEmpty() ? q.codec : q.label;
        if (!q.codec.trimmed().isEmpty() && !label.contains(q.codec, Qt::CaseInsensitive))
            label += QStringLiteral(" / ") + q.codec;
        parts << label;
    }
    return parts.join(QStringLiteral(", "));
}

QJsonObject Track::toJson() const
{
    QJsonObject obj;
    obj["id"] = id;
    obj["title"] = title;
    obj["artist"] = artist;
    obj["album"] = album;
    obj["genre"] = genre;
    obj["cover"] = coverPath;
    obj["lyrics"] = lyricPath;
    if (year > 0)
        obj["year"] = year;
    if (disc > 0)
        obj["disc"] = disc;
    if (trackNo > 0)
        obj["track"] = trackNo;

    QJsonArray arr;
    for (const auto &q : qualities)
        arr.append(q.toJson());
    obj["qualities"] = arr;
    return obj;
}

QVariantMap Track::toVariantMap() const
{
    QVariantList qs;
    for (const auto &q : qualities)
        qs << q.toVariantMap();

    return {
        {"id", id},
        {"title", displayTitle()},
        {"artist", artist},
        {"album", album},
        {"genre", genre},
        {"cover", coverPath},
        {"coverUrl", fileUrlFromPath(coverPath)},
        {"lyrics", lyricPath},
        {"lyricsUrl", fileUrlFromPath(lyricPath)},
        {"year", year},
        {"disc", disc},
        {"track", trackNo},
        {"primaryPath", primaryPath()},
        {"primaryUrl", fileUrlFromPath(primaryPath())},
        {"qualityCount", qualities.size()},
        {"qualitiesText", qualitiesText()},
        {"qualities", qs},
    };
}

Track Track::fromJson(const QJsonObject &obj)
{
    Track t;
    t.id = obj.value("id").toString();
    t.title = obj.value("title").toString();
    t.artist = obj.value("artist").toString();
    t.album = obj.value("album").toString();
    t.genre = obj.value("genre").toString();
    t.coverPath = canonicalLocalPath(obj.value("cover").toString());
    t.lyricPath = canonicalLocalPath(obj.value("lyrics").toString());
    t.year = obj.value("year").toInt();
    t.disc = obj.value("disc").toInt(1);
    t.trackNo = obj.value("track").toInt();

    const QJsonArray arr = obj.value("qualities").toArray();
    for (const auto &v : arr) {
        const auto q = TrackQuality::fromJson(v.toObject());
        if (!q.path.trimmed().isEmpty())
            t.qualities.push_back(q);
    }
    if (t.id.trimmed().isEmpty())
        t.id = stableTrackId(t);
    return t;
}

QString stableTrackId(const Track &track)
{
    const QByteArray input = (track.normalizedKey() + QStringLiteral("\n") + track.primaryPath()).toUtf8();
    return QString::fromLatin1(QCryptographicHash::hash(input, QCryptographicHash::Sha1).toHex().left(16));
}

