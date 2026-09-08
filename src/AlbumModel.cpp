#include "AlbumModel.h"
#include "LibraryManager.h"
#include "Track.h"
#include <QCollator>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QUuid>
#include <QUrl>
#include <QRegularExpression>
#include <algorithm>

static QString norm(const QString &text)
{
    return text.normalized(QString::NormalizationForm_C).simplified().toCaseFolded();
}

static QString albumDirectory(const Track &t)
{
    if (t.qualities.isEmpty()) return {};
    QDir directory(QFileInfo(t.qualities.first().path).absolutePath());
    static const QRegularExpression discFolder("^(?:cd|disc|disk)[ _-]*[0-9]+$", QRegularExpression::CaseInsensitiveOption);
    if (discFolder.match(directory.dirName()).hasMatch()) return QFileInfo(directory.absolutePath()).absolutePath();
    return directory.absolutePath();
}

static QString groupingKey(const Track &t, bool directoryCompilations, int effectiveYear = -1)
{
    if (!t.albumId.isEmpty()) return "id:" + t.albumId;
    // No file stats, image decodes or fingerprints in album identity. A missing
    // or changed cover never splits an album. Different editions retain years.
    const QString directory = albumDirectory(t);
    const QString name = norm(t.album);
    QString owner = norm(t.albumArtist.isEmpty() ? t.artist : t.albumArtist);
    if ((directoryCompilations && t.albumArtist.isEmpty()) || name.isEmpty() || owner.isEmpty())
        owner = "dir:" + directory;
    return owner + QChar(0x1f) + name + QChar(0x1f) + QString::number(effectiveYear < 0 ? t.year : effectiveYear);
}

AlbumModel::AlbumModel(LibraryManager *library, QObject *parent)
    : QAbstractListModel(parent), m_library(library)
{
    m_sortMode = qBound(0, QSettings().value("albums/sortMode", 0).toInt(), 3);
    m_autoMergeAlbums = QSettings().value("albums/groupDirectoryCompilations", false).toBool();
    if (m_library) {
        connect(m_library, &LibraryManager::libraryChanged, this, &AlbumModel::rebuild);
        rebuild();
    }
}

int AlbumModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_visibleAlbums.size();
}

void AlbumModel::setAutoMergeAlbums(bool enabled)
{
    if (m_autoMergeAlbums == enabled) return;
    m_autoMergeAlbums = enabled;
    QSettings().setValue("albums/groupDirectoryCompilations", enabled);
    rebuild();
    emit autoMergeAlbumsChanged();
}

void AlbumModel::setSortMode(int mode)
{
    if (mode < 0 || mode > 3 || mode == m_sortMode) return;
    m_sortMode = mode;
    QSettings().setValue("albums/sortMode", mode);
    rebuild();
    emit sortModeChanged();
}

void AlbumModel::setSearchQuery(const QString &query)
{
    if (m_searchQuery == norm(query)) return;
    beginResetModel();
    m_searchQuery = norm(query);
    filter();
    endResetModel();
    emit searchQueryChanged();
}

void AlbumModel::filter()
{
    m_visibleAlbums.clear();
    for (int i = 0; i < m_albums.size(); ++i) {
        const auto &a = m_albums[i];
        if (m_searchQuery.isEmpty() || norm(a.title + " " + a.artist + " " + a.searchText).contains(m_searchQuery))
            m_visibleAlbums.append(i);
    }
}

QVariant AlbumModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_visibleAlbums.size()) return {};
    const Album &a = m_albums[m_visibleAlbums[index.row()]];
    switch (role) {
    case KeyRole: return a.key;
    case ArtistRole: return a.artist;
    case AlbumRole: return a.title;
    case CountRole: return a.rows.size();
    case CoverUrlRole: return a.coverPath.isEmpty() ? QString() : QUrl::fromLocalFile(a.coverPath).toString();
    case YearRole: return a.year;
    case SubtitleRole: return QStringLiteral("%1 · %2 首").arg(a.artist).arg(a.rows.size());
    default: return {};
    }
}

QHash<int, QByteArray> AlbumModel::roleNames() const
{
    return {{KeyRole, "albumKey"}, {ArtistRole, "artist"}, {AlbumRole, "album"},
        {CountRole, "trackCount"}, {CoverUrlRole, "coverUrl"}, {YearRole, "year"}, {SubtitleRole, "subtitle"}};
}

int AlbumModel::indexOfKey(const QString &key) const
{
    return m_visibleAlbums.indexOf(m_byKey.value(key, -1));
}

QVariantMap AlbumModel::info(const QString &key) const
{
    const auto i = m_byKey.constFind(key);
    if (i == m_byKey.cend()) return {};
    const auto &a = m_albums[*i];
    return {{"key", a.key}, {"album", a.title}, {"artist", a.artist}, {"year", a.year},
        {"grouping", a.key.startsWith("id:") ? QStringLiteral("手动归组 · 已保存") : a.key.startsWith("dir:") ? QStringLiteral("按目录、专辑名与年份归组") : QStringLiteral("按专辑艺术家、名称与年份归组")},
        {"trackCount", a.rows.size()}, {"coverUrl", a.coverPath.isEmpty() ? QString() : QUrl::fromLocalFile(a.coverPath).toString()}};
}

QVariantList AlbumModel::tracks(int albumIndex) const
{
    QVariantList out;
    if (!m_library || albumIndex < 0 || albumIndex >= m_visibleAlbums.size()) return out;
    for (int row : m_albums[m_visibleAlbums[albumIndex]].rows) {
        QVariantMap item = m_library->track(row);
        item["libraryRow"] = row;
        out.append(item);
    }
    return out;
}

int AlbumModel::firstTrackRow(int albumIndex) const
{
    if (albumIndex < 0 || albumIndex >= m_visibleAlbums.size()) return -1;
    const auto &rows = m_albums[m_visibleAlbums[albumIndex]].rows;
    return rows.isEmpty() ? -1 : rows.first();
}

QStringList AlbumModel::trackIds(const QString &key) const
{
    QStringList ids;
    const auto i = m_byKey.constFind(key);
    if (!m_library || i == m_byKey.cend()) return ids;
    for (int row : m_albums[*i].rows) ids.append(m_library->trackAt(row)->id);
    return ids;
}

QVariantList AlbumModel::choices(const QString &exceptKey, const QString &query) const
{
    QVariantList matching, others;
    const QString title = norm(info(exceptKey).value("album").toString());
    for (const auto &a : m_albums) {
        if (a.key == exceptKey || (!query.trimmed().isEmpty() && !norm(a.title + " " + a.artist + " " + QString::number(a.year)).contains(norm(query)))) continue;
        QVariantMap item {{"key", a.key}, {"label", QStringLiteral("%1 · %2 · %3 · %4 首").arg(a.title, a.artist, a.year ? QString::number(a.year) : QStringLiteral("年份未知")).arg(a.rows.size())},
            {"count", a.rows.size()}};
        (norm(a.title) == title ? matching : others).append(item);
    }
    return matching + others;
}

QString AlbumModel::editAlbum(const QString &key, const QString &title, const QString &artist, int year)
{
    return m_library->updateAlbum(trackIds(key), title, artist, year,
        key.startsWith("id:") ? key.mid(3) : QUuid::createUuid().toString(QUuid::WithoutBraces));
}

QString AlbumModel::mergeAlbums(const QString &sourceKey, const QString &targetKey)
{
    if (sourceKey == targetKey) return QStringLiteral("请选择另一个专辑");
    const auto source = trackIds(sourceKey), target = trackIds(targetKey);
    if (source.isEmpty() || target.isEmpty()) return QStringLiteral("专辑已变化，请重新选择");
    const auto destination = info(targetKey);
    const QString artist = destination.value("artist").toString();
    return m_library->updateAlbum(source + target, destination.value("album").toString(), artist,
        destination.value("year").toInt(), targetKey.startsWith("id:") ? targetKey.mid(3) : QUuid::createUuid().toString(QUuid::WithoutBraces));
}

QString AlbumModel::removeAlbum(const QString &key)
{
    return m_library->removeTracks(trackIds(key));
}

QVariantList AlbumModel::tracksForKey(const QString &key) const
{
    QVariantList out;
    for (const auto &id : trackIds(key)) {
        const int row = m_library->rowOfId(id);
        auto item = m_library->track(row);
        item["libraryRow"] = row;
        out.append(item);
    }
    return out;
}

QString AlbumModel::moveTracks(const QString &sourceKey, const QStringList &ids, const QString &targetKey)
{
    if (sourceKey == targetKey || ids.isEmpty()) return QStringLiteral("请选择曲目和不同的目标专辑");
    const auto available = trackIds(sourceKey), target = trackIds(targetKey);
    if (target.isEmpty()) return QStringLiteral("目标专辑已变化，请重新选择");
    for (const auto &id : ids) if (!available.contains(id)) return QStringLiteral("曲目归属已变化，请重新选择");
    const auto destination = info(targetKey);
    return m_library->updateAlbum(ids + target, destination.value("album").toString(), destination.value("artist").toString(),
        destination.value("year").toInt(), targetKey.startsWith("id:") ? targetKey.mid(3) : QUuid::createUuid().toString(QUuid::WithoutBraces));
}

QString AlbumModel::splitTracks(const QString &sourceKey, const QStringList &ids, const QString &title, const QString &artist, int year)
{
    const auto available = trackIds(sourceKey);
    if (ids.isEmpty() || ids.size() >= available.size()) return QStringLiteral("拆分需要选择部分曲目；修改整张专辑请使用编辑");
    for (const auto &id : ids) if (!available.contains(id)) return QStringLiteral("曲目归属已变化，请重新选择");
    return m_library->updateAlbum(ids, title, artist, year, QUuid::createUuid().toString(QUuid::WithoutBraces));
}

void AlbumModel::rebuild()
{
    if (!m_library) return;
    QVector<Album> next;
    QHash<QString, int> byKey;
    const auto &tracks = m_library->tracks();
    QHash<QString, QSet<int>> knownYears;
    auto editionScope = [&](const Track &t) { return groupingKey(t, m_autoMergeAlbums, 0) + QChar(0x1e) + albumDirectory(t); };
    for (const auto &t : tracks) if (t.albumId.isEmpty() && t.year > 0) knownYears[editionScope(t)].insert(t.year);
    for (int row = 0; row < tracks.size(); ++row) {
        const auto &t = tracks[row];
        int year = t.year;
        if (t.albumId.isEmpty() && !year) {
            const auto years = knownYears.value(editionScope(t));
            if (years.size() == 1) year = *years.cbegin();
        }
        const QString key = groupingKey(t, m_autoMergeAlbums, year);
        auto it = byKey.constFind(key);
        int index;
        if (it == byKey.cend()) {
            index = next.size();
            byKey.insert(key, index);
            Album a;
            a.key = key;
            a.title = t.album.isEmpty() ? QStringLiteral("未命名专辑") : t.album;
            a.year = year;
            next.append(a);
        } else index = *it;
        auto &a = next[index];
        a.rows.append(row);
        a.searchText += " " + t.artist + " " + t.title;
        const QString artist = t.albumArtist.isEmpty() ? t.artist : t.albumArtist;
        if (!artist.isEmpty()) a.artists.insert(artist);
        if (a.coverPath.isEmpty()) a.coverPath = t.coverPath;
    }
    QCollator collator;
    collator.setNumericMode(true);
    for (auto &a : next) {
        a.artist = a.artists.size() > 1 ? QStringLiteral("Various Artists")
            : a.artists.isEmpty() ? QStringLiteral("未知艺术家") : *a.artists.cbegin();
        std::stable_sort(a.rows.begin(), a.rows.end(), [&](int left, int right) {
            const auto &x = tracks[left], &y = tracks[right];
            if (x.disc != y.disc) return x.disc < y.disc;
            if (x.trackNo != y.trackNo) return (x.trackNo > 0 ? x.trackNo : 999999) < (y.trackNo > 0 ? y.trackNo : 999999);
            return collator.compare(x.title, y.title) < 0;
        });
    }
    std::sort(next.begin(), next.end(), [&](const Album &a, const Album &b) {
        if (m_sortMode == 1 && a.artist != b.artist) return collator.compare(a.artist, b.artist) < 0;
        if (m_sortMode == 2 && a.year != b.year) return a.year > b.year;
        if (m_sortMode == 3 && a.rows.size() != b.rows.size()) return a.rows.size() > b.rows.size();
        int order = collator.compare(a.title, b.title);
        if (order != 0) return order < 0;
        order = collator.compare(a.artist, b.artist);
        if (order != 0) return order < 0;
        if (a.year != b.year) return a.year < b.year;
        return a.key < b.key;
    });
    beginResetModel();
    m_albums = std::move(next);
    m_byKey.clear();
    for (int i = 0; i < m_albums.size(); ++i) m_byKey.insert(m_albums[i].key, i);
    filter();
    endResetModel();
    emit albumsChanged();
}
