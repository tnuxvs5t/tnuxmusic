#include "LibraryManager.h"

#include "MetadataReader.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>

static const QSet<QString> kAudioExt = {
    "mp3", "flac", "wav", "m4a", "aac", "ogg", "opus", "wma", "aiff", "alac"
};

static const QStringList kCoverNames = {
    "cover", "folder", "front", "album", "artwork"
};

static const QStringList kImageExt = {
    "jpg", "jpeg", "png", "webp", "bmp"
};

static QString cleanTitle(QString s)
{
    s.replace('_', ' ');
    static const QRegularExpression qualitySuffix(
        R"(\s*[\[(](flac|mp3|lossless|hi[- ]?res|24bit|16bit|320k|256k|192k|128k|hq|sq)[\])]\s*$)",
        QRegularExpression::CaseInsensitiveOption);
    s.remove(qualitySuffix);
    return s.simplified();
}

static QString qualityLabelFor(const QFileInfo &info)
{
    const QString ext = info.suffix().toLower();
    const QString name = info.completeBaseName().toLower();
    if (ext == "flac" || ext == "alac" || ext == "wav" || ext == "aiff")
        return QStringLiteral("Lossless");
    if (name.contains("320"))
        return QStringLiteral("MP3 320k");
    if (name.contains("256"))
        return QStringLiteral("AAC/MP3 256k");
    if (name.contains("128"))
        return QStringLiteral("MP3 128k");
    return ext.toUpper();
}

static QString findSidecar(const QFileInfo &audio, const QStringList &suffixes, const QStringList &baseNames)
{
    const QDir dir = audio.dir();
    const QString stem = audio.completeBaseName();

    for (const QString &ext : suffixes) {
        const QString sameStem = dir.filePath(stem + "." + ext);
        if (QFileInfo::exists(sameStem))
            return canonicalLocalPath(sameStem);
    }

    for (const QString &base : baseNames) {
        for (const QString &ext : suffixes) {
            const QString candidate = dir.filePath(base + "." + ext);
            if (QFileInfo::exists(candidate))
                return canonicalLocalPath(candidate);
        }
    }
    return {};
}

LibraryManager::LibraryManager(QObject *parent)
    : QAbstractListModel(parent)
{
    const QString root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(root);
    m_libraryPath = QDir(root).filePath("library.json");
}

int LibraryManager::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_tracks.size();
}

QVariant LibraryManager::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_tracks.size())
        return {};

    const Track &t = m_tracks[index.row()];
    switch (role) {
    case IdRole: return t.id;
    case TitleRole: return t.displayTitle();
    case ArtistRole: return t.artist;
    case AlbumRole: return t.album;
    case GenreRole: return t.genre;
    case YearRole: return t.year;
    case CoverPathRole: return t.coverPath;
    case CoverUrlRole: return fileUrlFromPath(t.coverPath);
    case LyricPathRole: return t.lyricPath;
    case PrimaryPathRole: return t.primaryPath();
    case PrimaryUrlRole: return fileUrlFromPath(t.primaryPath());
    case QualityCountRole: return t.qualities.size();
    case QualitiesTextRole: return t.qualitiesText();
    default: return {};
    }
}

QHash<int, QByteArray> LibraryManager::roleNames() const
{
    return {
        {IdRole, "trackId"},
        {TitleRole, "title"},
        {ArtistRole, "artist"},
        {AlbumRole, "album"},
        {GenreRole, "genre"},
        {YearRole, "year"},
        {CoverPathRole, "coverPath"},
        {CoverUrlRole, "coverUrl"},
        {LyricPathRole, "lyricPath"},
        {PrimaryPathRole, "primaryPath"},
        {PrimaryUrlRole, "primaryUrl"},
        {QualityCountRole, "qualityCount"},
        {QualitiesTextRole, "qualitiesText"},
    };
}

QString LibraryManager::loadDefault()
{
    if (!QFileInfo::exists(m_libraryPath)) {
        setLastMessage(QStringLiteral("默认曲库尚未创建：%1").arg(m_libraryPath));
        return m_lastMessage;
    }

    QJsonObject obj;
    QString error;
    if (!readJsonFile(m_libraryPath, &obj, &error)) {
        setLastMessage(error);
        return error;
    }
    if (!replaceFromJsonObject(obj, &error)) {
        setLastMessage(error);
        return error;
    }
    setLastMessage(QStringLiteral("已加载默认曲库：%1 首").arg(m_tracks.size()));
    return m_lastMessage;
}

QString LibraryManager::save()
{
    QString error;
    if (!writeJsonFile(m_libraryPath, toJsonObject(), &error)) {
        setLastMessage(error);
        return error;
    }
    setLastMessage(QStringLiteral("已保存默认曲库：%1").arg(m_libraryPath));
    return m_lastMessage;
}

QString LibraryManager::scanFolder(const QString &folderUrl)
{
    const QString folder = canonicalLocalPath(folderUrl);
    QFileInfo fi(folder);
    if (!fi.exists() || !fi.isDir()) {
        const QString msg = QStringLiteral("扫描失败：不是文件夹 %1").arg(folder);
        setLastMessage(msg);
        return msg;
    }

    int addedBefore = m_tracks.size();
    int audioFiles = 0;
    beginResetModel();
    QDirIterator it(folder, QDir::Files | QDir::Readable, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const QFileInfo info(path);
        if (!kAudioExt.contains(info.suffix().toLower()))
            continue;
        ++audioFiles;
        mergeTrack(inferTrackFromAudioFile(path));
    }
    endResetModel();
    emit libraryChanged();

    save();
    const int added = m_tracks.size() - addedBefore;
    setLastMessage(QStringLiteral("扫描完成：发现 %1 个音频文件，新增 %2 首，曲库共 %3 首")
                       .arg(audioFiles).arg(added).arg(m_tracks.size()));
    return m_lastMessage;
}

QString LibraryManager::importLibrary(const QString &fileUrl)
{
    const QString path = canonicalLocalPath(fileUrl);
    QJsonObject obj;
    QString error;
    if (!readJsonFile(path, &obj, &error)) {
        setLastMessage(error);
        return error;
    }
    if (!replaceFromJsonObject(obj, &error)) {
        setLastMessage(error);
        return error;
    }
    save();
    setLastMessage(QStringLiteral("已导入曲库：%1 首").arg(m_tracks.size()));
    return m_lastMessage;
}

QString LibraryManager::mergeLibrary(const QString &fileUrl)
{
    const QString path = canonicalLocalPath(fileUrl);
    QJsonObject obj;
    QString error;
    if (!readJsonFile(path, &obj, &error)) {
        setLastMessage(error);
        return error;
    }
    const int before = m_tracks.size();
    beginResetModel();
    const bool ok = mergeFromJsonObject(obj, &error);
    endResetModel();
    if (!ok) {
        setLastMessage(error);
        return error;
    }
    emit libraryChanged();
    save();
    setLastMessage(QStringLiteral("已合并曲库：新增 %1 首，共 %2 首").arg(m_tracks.size() - before).arg(m_tracks.size()));
    return m_lastMessage;
}

QString LibraryManager::exportLibrary(const QString &fileUrl) const
{
    const QString path = canonicalLocalPath(fileUrl);
    QString error;
    if (!writeJsonFile(path, toJsonObject(), &error))
        return error;
    return QStringLiteral("已导出曲库：%1").arg(path);
}

const Track *LibraryManager::trackAt(int row) const
{
    if (row < 0 || row >= m_tracks.size())
        return nullptr;
    return &m_tracks[row];
}

int LibraryManager::rowOfId(const QString &id) const
{
    for (int i = 0; i < m_tracks.size(); ++i) {
        if (m_tracks[i].id == id)
            return i;
    }
    return -1;
}

QVariantMap LibraryManager::track(int row) const
{
    if (row < 0 || row >= m_tracks.size())
        return {};
    return m_tracks[row].toVariantMap();
}

int LibraryManager::rowOfTrackId(const QString &id) const
{
    return rowOfId(id);
}

QString LibraryManager::primaryPath(int row) const
{
    if (row < 0 || row >= m_tracks.size())
        return {};
    return m_tracks[row].primaryPath();
}

QString LibraryManager::lyricPath(int row) const
{
    if (row < 0 || row >= m_tracks.size())
        return {};
    return m_tracks[row].lyricPath;
}

QString LibraryManager::coverPath(int row) const
{
    if (row < 0 || row >= m_tracks.size())
        return {};
    return m_tracks[row].coverPath;
}

void LibraryManager::clear()
{
    beginResetModel();
    m_tracks.clear();
    endResetModel();
    emit libraryChanged();
    setLastMessage(QStringLiteral("已清空当前曲库"));
}

QJsonObject LibraryManager::toJsonObject() const
{
    QJsonObject root;
    root["schema"] = QStringLiteral("tnuxmusic.library.v1");
    root["app"] = QStringLiteral("tnuxmusic");
    root["version"] = 1;

    QJsonArray arr;
    for (const auto &t : m_tracks)
        arr.append(t.toJson());
    root["tracks"] = arr;
    return root;
}

bool LibraryManager::replaceFromJsonObject(const QJsonObject &obj, QString *error)
{
    const QJsonArray arr = obj.value("tracks").toArray();
    QVector<Track> next;
    next.reserve(arr.size());
    for (const auto &v : arr) {
        Track t = Track::fromJson(v.toObject());
        if (!t.qualities.isEmpty())
            next.push_back(t);
    }

    beginResetModel();
    m_tracks.clear();
    for (const auto &t : next)
        mergeTrack(t);
    endResetModel();
    emit libraryChanged();

    if (error)
        error->clear();
    return true;
}

bool LibraryManager::mergeFromJsonObject(const QJsonObject &obj, QString *error)
{
    const QJsonArray arr = obj.value("tracks").toArray();
    for (const auto &v : arr) {
        Track t = Track::fromJson(v.toObject());
        if (!t.qualities.isEmpty())
            mergeTrack(t);
    }
    if (error)
        error->clear();
    return true;
}

void LibraryManager::setLastMessage(const QString &message)
{
    if (m_lastMessage == message)
        return;
    m_lastMessage = message;
    emit lastMessageChanged();
}

bool LibraryManager::readJsonFile(const QString &path, QJsonObject *out, QString *error) const
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("无法读取 JSON：%1").arg(path);
        return false;
    }
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error)
            *error = QStringLiteral("JSON 格式错误：%1 (%2)").arg(path, pe.errorString());
        return false;
    }
    *out = doc.object();
    return true;
}

bool LibraryManager::writeJsonFile(const QString &path, const QJsonObject &obj, QString *error) const
{
    const QFileInfo info(path);
    if (!info.absoluteDir().exists())
        QDir().mkpath(info.absolutePath());

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error)
            *error = QStringLiteral("无法写入 JSON：%1").arg(path);
        return false;
    }
    f.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    return true;
}

void LibraryManager::mergeTrack(const Track &track)
{
    if (track.qualities.isEmpty())
        return;

    const QString key = track.normalizedKey();
    for (auto &t : m_tracks) {
        if (t.normalizedKey() != key)
            continue;

        QSet<QString> paths;
        for (const auto &q : t.qualities)
            paths.insert(canonicalLocalPath(q.path));
        for (const auto &q : track.qualities) {
            if (!paths.contains(canonicalLocalPath(q.path)))
                t.qualities.push_back(q);
        }
        if (t.coverPath.isEmpty())
            t.coverPath = track.coverPath;
        if (t.lyricPath.isEmpty())
            t.lyricPath = track.lyricPath;
        if (t.genre.isEmpty())
            t.genre = track.genre;
        if (t.year == 0)
            t.year = track.year;
        if (t.trackNo == 0)
            t.trackNo = track.trackNo;
        if (t.disc <= 1)
            t.disc = track.disc;
        if (t.id.isEmpty())
            t.id = stableTrackId(t);
        return;
    }

    Track t = track;
    if (t.id.isEmpty())
        t.id = stableTrackId(t);
    m_tracks.push_back(t);
}

Track LibraryManager::inferTrackFromAudioFile(const QString &path) const
{
    const QFileInfo info(path);
    Track t;
    const AudioMetadata meta = MetadataReader::read(path);

    t.title = meta.title.isEmpty() ? cleanTitle(info.completeBaseName()) : meta.title;
    t.album = meta.album.isEmpty() ? info.dir().dirName() : meta.album;
    t.artist = meta.artist;
    t.genre = meta.genre;
    t.year = meta.year;
    t.trackNo = meta.trackNo;
    t.disc = meta.disc > 0 ? meta.disc : 1;

    QDir artistDir = info.dir();
    if (t.artist.isEmpty() && artistDir.cdUp())
        t.artist = artistDir.dirName();

    TrackQuality q;
    q.path = canonicalLocalPath(path);
    q.codec = info.suffix().toUpper();
    q.label = qualityLabelFor(info);
    t.qualities.push_back(q);

    t.coverPath = findSidecar(info, kImageExt, kCoverNames);
    t.lyricPath = findSidecar(info, {"tly"}, {});
    t.id = stableTrackId(t);
    return t;
}
