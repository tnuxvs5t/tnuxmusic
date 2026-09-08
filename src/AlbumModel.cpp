#include "AlbumModel.h"
#include "LibraryManager.h"
#include "Track.h"
#include <QCollator>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QUuid>
#include <QUrl>
#include <algorithm>

static QString norm(const QString &text)
{
    return text.normalized(QString::NormalizationForm_C).simplified().toCaseFolded();
}

static QString groupingKey(const Track &t, bool directoryCompilations)
{
    if (!t.albumId.isEmpty()) return "id:" + t.albumId;
    // No file stats, image decodes or fingerprints in album identity. A missing
    // or changed cover never splits an album. Different editions retain years.
    const QString directory = t.qualities.isEmpty() ? QString() : QFileInfo(t.qualities.first().path).absolutePath();
    const QString name = norm(t.album);
    QString owner = norm(t.albumArtist.isEmpty() ? t.artist : t.albumArtist);
    if ((directoryCompilations && t.albumArtist.isEmpty()) || name.isEmpty() || owner.isEmpty())
        owner = "dir:" + directory;
    return owner + QChar(0x1f) + name + QChar(0x1f) + QString::number(t.year);
}

AlbumModel::AlbumModel(LibraryManager *library, QObject *parent)
    : QAbstractListModel(parent), m_library(library)
{
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
        if (m_searchQuery.isEmpty() || norm(a.title + " " + a.artist).contains(m_searchQuery))
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

QVariantList AlbumModel::choices(const QString &exceptKey) const
{
    QVariantList matching, others;
    const QString title = norm(info(exceptKey).value("album").toString());
    for (const auto &a : m_albums) {
        if (a.key == exceptKey) continue;
        QVariantMap item {{"key", a.key}, {"label", QStringLiteral("%1 · %2 · %3 首").arg(a.title, a.artist).arg(a.rows.size())},
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
    const QString artist = info(sourceKey).value("artist") == destination.value("artist")
        ? destination.value("artist").toString() : QStringLiteral("Various Artists");
    return m_library->updateAlbum(source + target, destination.value("album").toString(), artist,
        destination.value("year").toInt(), targetKey.startsWith("id:") ? targetKey.mid(3) : QUuid::createUuid().toString(QUuid::WithoutBraces));
}

QString AlbumModel::removeAlbum(const QString &key)
{
    return m_library->removeTracks(trackIds(key));
}

void AlbumModel::rebuild()
{
    if (!m_library) return;
    QVector<Album> next;
    QHash<QString, int> byKey;
    const auto &tracks = m_library->tracks();
    for (int row = 0; row < tracks.size(); ++row) {
        const auto &t = tracks[row];
        const QString key = groupingKey(t, m_autoMergeAlbums);
        auto it = byKey.constFind(key);
        int index;
        if (it == byKey.cend()) {
            index = next.size();
            byKey.insert(key, index);
            Album a;
            a.key = key;
            a.title = t.album.isEmpty() ? QStringLiteral("未命名专辑") : t.album;
            a.year = t.year;
            next.append(a);
        } else index = *it;
        auto &a = next[index];
        a.rows.append(row);
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
