#include "AlbumModel.h"

#include "LibraryManager.h"
#include "Track.h"

#include <QCollator>
#include <QHash>
#include <QImage>
#include <algorithm>

static QString albumKey(const Track &t)
{
    return t.artist.simplified().toCaseFolded() + QChar(0x1f) + t.album.simplified().toCaseFolded();
}

static QString coverImageHash(const QString &path)
{
    if (path.trimmed().isEmpty())
        return {};

    QImage image(path);
    if (image.isNull())
        return {};

    const QImage small = image.convertToFormat(QImage::Format_Grayscale8).scaled(8, 8, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    if (small.isNull())
        return {};

    int total = 0;
    QVector<int> pixels;
    pixels.reserve(64);
    for (int y = 0; y < small.height(); ++y) {
        for (int x = 0; x < small.width(); ++x) {
            const int value = qGray(small.pixel(x, y));
            pixels.push_back(value);
            total += value;
        }
    }

    const int average = total / pixels.size();
    quint64 bits = 0;
    for (int value : pixels)
        bits = (bits << 1) | (value >= average ? 1ULL : 0ULL);

    return QString::number(bits, 16).rightJustified(16, QLatin1Char('0'));
}

static QString mergedAlbumKey(const Track &t)
{
    const QString imageHash = coverImageHash(t.coverPath);
    if (imageHash.isEmpty())
        return albumKey(t);
    return imageHash + QChar(0x1f) + t.album.simplified().toCaseFolded();
}

AlbumModel::AlbumModel(LibraryManager *library, QObject *parent)
    : QAbstractListModel(parent)
    , m_library(library)
{
    if (m_library) {
        connect(m_library, &LibraryManager::libraryChanged, this, &AlbumModel::rebuild);
        rebuild();
    }
}

int AlbumModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_albums.size();
}

void AlbumModel::setAutoMergeAlbums(bool enabled)
{
    if (m_autoMergeAlbums == enabled)
        return;
    m_autoMergeAlbums = enabled;
    rebuild();
    emit autoMergeAlbumsChanged();
}

QVariant AlbumModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_albums.size())
        return {};

    const Album &a = m_albums[index.row()];
    switch (role) {
    case KeyRole: return a.key;
    case ArtistRole: return a.artist;
    case AlbumRole: return a.title;
    case CountRole: return a.rows.size();
    case CoverUrlRole: return fileUrlFromPath(a.coverPath);
    case YearRole: return a.year;
    case SubtitleRole:
        return QStringLiteral("%1 · %2 首")
            .arg(a.artist.isEmpty() ? QStringLiteral("未知艺术家") : a.artist)
            .arg(a.rows.size());
    default: return {};
    }
}

QHash<int, QByteArray> AlbumModel::roleNames() const
{
    return {
        {KeyRole, "albumKey"},
        {ArtistRole, "artist"},
        {AlbumRole, "album"},
        {CountRole, "trackCount"},
        {CoverUrlRole, "coverUrl"},
        {YearRole, "year"},
        {SubtitleRole, "subtitle"},
    };
}

QVariantList AlbumModel::tracks(int albumIndex) const
{
    QVariantList out;
    if (!m_library || albumIndex < 0 || albumIndex >= m_albums.size())
        return out;

    for (int row : m_albums[albumIndex].rows) {
        QVariantMap item = m_library->track(row);
        item["libraryRow"] = row;
        out << item;
    }
    return out;
}

int AlbumModel::firstTrackRow(int albumIndex) const
{
    if (albumIndex < 0 || albumIndex >= m_albums.size() || m_albums[albumIndex].rows.isEmpty())
        return -1;
    return m_albums[albumIndex].rows.first();
}

void AlbumModel::rebuild()
{
    if (!m_library)
        return;

    QVector<Album> next;
    QHash<QString, int> byKey;
    const auto &tracks = m_library->tracks();

    for (int row = 0; row < tracks.size(); ++row) {
        const Track &t = tracks[row];
        const QString key = m_autoMergeAlbums ? mergedAlbumKey(t) : albumKey(t);
        int idx = byKey.value(key, -1);
        if (idx < 0) {
            Album a;
            a.key = key;
            a.artist = t.artist;
            a.title = t.album.isEmpty() ? QStringLiteral("Unknown Album") : t.album;
            a.coverPath = t.coverPath;
            a.year = t.year;
            idx = next.size();
            byKey.insert(key, idx);
            next.push_back(a);
        }

        Album &a = next[idx];
        a.rows.push_back(row);
        if (m_autoMergeAlbums && !t.artist.trimmed().isEmpty() &&
            !a.artist.split(QStringLiteral(" / ")).contains(t.artist)) {
            a.artist = a.artist.trimmed().isEmpty() ? t.artist : a.artist + QStringLiteral(" / ") + t.artist;
        }
        if (a.coverPath.isEmpty())
            a.coverPath = t.coverPath;
        if (a.year == 0)
            a.year = t.year;
    }

    QCollator collator;
    collator.setNumericMode(true);
    std::sort(next.begin(), next.end(), [&](const Album &a, const Album &b) {
        const int aa = collator.compare(a.artist, b.artist);
        if (aa != 0)
            return aa < 0;
        const int ab = collator.compare(a.title, b.title);
        if (ab != 0)
            return ab < 0;
        return a.year < b.year;
    });

    beginResetModel();
    m_albums = next;
    endResetModel();
    emit albumsChanged();
}
