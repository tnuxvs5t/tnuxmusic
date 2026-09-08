#include "PlaybackQueue.h"

#include "LibraryManager.h"
#include "Track.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QSaveFile>
#include <QSettings>
#include <QRandomGenerator>
#include <QUrl>
#include <algorithm>

PlaybackQueue::PlaybackQueue(LibraryManager *library, QObject *parent)
    : QAbstractListModel(parent)
    , m_library(library)
{
    const QString root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(root);
    m_path = QDir(root).filePath("playlists.json");
    loadPlaylists();
    m_playbackMode = std::clamp(QSettings().value("player/mode", RepeatAll).toInt(), 0, 3);

    if (m_library) {
        connect(m_library, &LibraryManager::libraryChanged, this, [this] {
            int nextCurrent = -1, retained = 0, nextResume = 0;
            for (int i = 0; i < m_queue.size(); ++i) {
                if (m_library->rowOfId(m_queue[i]) < 0) continue;
                if (i == m_currentIndex) nextCurrent = retained;
                if (i < (m_currentIndex >= 0 ? m_currentIndex : m_resumeIndex)) ++nextResume;
                ++retained;
            }
            beginResetModel();
            m_queue.removeIf( [this](const QString &id) { return m_library->rowOfId(id) < 0; });
            m_currentIndex = nextCurrent;
            m_resumeIndex = nextResume;
            endResetModel();
            emit queueChanged();
            emit currentIndexChanged();
        });
    }
}

int PlaybackQueue::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_queue.size();
}

QVariant PlaybackQueue::data(const QModelIndex &idx, int role) const
{
    if (!idx.isValid() || idx.row() < 0 || idx.row() >= m_queue.size() || !m_library)
        return {};

    const int q = idx.row();
    const int row = rowForTrackId(m_queue[q]);
    const Track *t = m_library->trackAt(row);
    switch (role) {
    case QueueIndexRole: return q;
    case LibraryRowRole: return row;
    case TitleRole: return t ? t->displayTitle() : QStringLiteral("曲目已不在曲库中");
    case ArtistRole: return t ? t->artist : QString();
    case AlbumRole: return t ? t->album : QString();
    case CoverUrlRole: return t && !t->coverPath.isEmpty() ? QUrl::fromLocalFile(t->coverPath).toString() : QString();
    case ActiveRole: return q == m_currentIndex;
    case TrackIdRole: return m_queue[q];
    default: return {};
    }
}

QHash<int, QByteArray> PlaybackQueue::roleNames() const
{
    return {
        {QueueIndexRole, "queueIndex"},
        {LibraryRowRole, "libraryRow"},
        {TitleRole, "title"},
        {ArtistRole, "artist"},
        {AlbumRole, "album"},
        {CoverUrlRole, "coverUrl"},
        {ActiveRole, "active"},
        {TrackIdRole, "trackId"},
    };
}

int PlaybackQueue::currentRow() const
{
    if (m_currentIndex < 0 || m_currentIndex >= m_queue.size())
        return -1;
    return rowForTrackId(m_queue[m_currentIndex]);
}

QStringList PlaybackQueue::playlistNames() const
{
    QStringList names = m_playlists.keys();
    names.sort(Qt::CaseInsensitive);
    return names;
}

int PlaybackQueue::enqueueRow(int libraryRow)
{
    const QString id = trackIdForRow(libraryRow);
    if (id.isEmpty())
        return -1;

    const int pos = m_queue.size();
    beginInsertRows({}, pos, pos);
    m_queue.push_back(id);
    endInsertRows();
    emit queueChanged();
    setLastMessage(QStringLiteral("已加入播放队列"));
    return pos;
}

int PlaybackQueue::playNowRow(int libraryRow)
{
    const QString id = trackIdForRow(libraryRow);
    if (id.isEmpty())
        return -1;

    for (int i = 0; i < m_queue.size(); ++i) {
        if (m_queue[i] == id) {
            setCurrentIndex(i);
            return libraryRow;
        }
    }

    const int insertAt = m_currentIndex >= 0 ? m_currentIndex + 1 : m_queue.size();
    beginInsertRows({}, insertAt, insertAt);
    m_queue.insert(insertAt, id);
    endInsertRows();
    emit queueChanged();
    setCurrentIndex(insertAt);
    return libraryRow;
}

int PlaybackQueue::activate(int queueIndex)
{
    if (queueIndex < 0 || queueIndex >= m_queue.size())
        return -1;
    setCurrentIndex(queueIndex);
    return currentRow();
}

void PlaybackQueue::setPlaybackMode(int mode)
{
    if (mode < Sequential || mode > Shuffle || mode == m_playbackMode) return;
    m_playbackMode = mode;
    QSettings().setValue("player/mode", mode);
    emit playbackModeChanged();
}

int PlaybackQueue::next(bool automatic)
{
    if (m_queue.isEmpty()) return -1;
    if (automatic && m_playbackMode == RepeatOne && m_currentIndex >= 0) return currentRow();
    int nextIndex = m_currentIndex < 0 ? m_resumeIndex : m_currentIndex + 1;
    if (m_playbackMode == Shuffle && m_currentIndex >= 0 && m_queue.size() > 1) {
        nextIndex = QRandomGenerator::global()->bounded(int(m_queue.size()) - 1);
        if (nextIndex >= m_currentIndex) ++nextIndex;
    }
    if (nextIndex >= m_queue.size()) {
        if (m_playbackMode == Sequential) return -1;
        nextIndex = 0;
    }
    setCurrentIndex(nextIndex);
    return currentRow();
}

int PlaybackQueue::previous()
{
    if (m_queue.isEmpty())
        return -1;
    const int cursor = m_currentIndex >= 0 ? m_currentIndex : m_resumeIndex;
    const int prevIndex = cursor <= 0 ? m_queue.size() - 1 : cursor - 1;
    setCurrentIndex(prevIndex);
    return currentRow();
}

void PlaybackQueue::removeAt(int queueIndex)
{
    if (queueIndex < 0 || queueIndex >= m_queue.size())
        return;

    beginRemoveRows({}, queueIndex, queueIndex);
    m_queue.removeAt(queueIndex);
    endRemoveRows();

    if (queueIndex == m_currentIndex) {
        m_currentIndex = -1;
        m_resumeIndex = queueIndex;
    } else if (queueIndex < m_currentIndex) --m_currentIndex;
    else if (m_currentIndex < 0 && queueIndex < m_resumeIndex) --m_resumeIndex;
    if (m_queue.isEmpty()) { m_currentIndex = -1; m_resumeIndex = 0; }

    if (!m_queue.isEmpty()) emit dataChanged(index(0), index(m_queue.size()-1), {ActiveRole});
    emit queueChanged();
    emit currentIndexChanged();
}

void PlaybackQueue::clear()
{
    beginResetModel();
    m_queue.clear();
    m_currentIndex = -1;
    m_resumeIndex = 0;
    endResetModel();
    emit queueChanged();
    emit currentIndexChanged();
    setLastMessage(QStringLiteral("播放队列已清空"));
}

QString PlaybackQueue::createPlaylist(const QString &name)
{
    const auto previous = m_playlists;
    const QString n = name.simplified();
    if (n.isEmpty())
        return QStringLiteral("歌单名不能为空");
    if (!m_playlists.contains(n))
        m_playlists.insert(n, {});
    if (!savePlaylists()) { m_playlists = previous; return m_lastMessage; }
    emit playlistsChanged();
    setLastMessage(QStringLiteral("已创建歌单：%1").arg(n));
    return m_lastMessage;
}

QString PlaybackQueue::deletePlaylist(const QString &name)
{
    const auto previous = m_playlists;
    const QString n = name.simplified();
    if (!m_playlists.remove(n))
        return QStringLiteral("歌单不存在：%1").arg(n);
    if (!savePlaylists()) { m_playlists = previous; return m_lastMessage; }
    emit playlistsChanged();
    setLastMessage(QStringLiteral("已删除歌单：%1").arg(n));
    return m_lastMessage;
}

QString PlaybackQueue::addRowToPlaylist(const QString &name, int libraryRow)
{
    const auto previous = m_playlists;
    const QString n = name.simplified();
    const QString id = trackIdForRow(libraryRow);
    if (n.isEmpty())
        return QStringLiteral("先输入或选择歌单名");
    if (id.isEmpty())
        return QStringLiteral("无法加入歌单：曲目无效");

    auto &tracks = m_playlists[n];
    if (!tracks.contains(id))
        tracks.push_back(id);
    if (!savePlaylists()) { m_playlists = previous; return m_lastMessage; }
    emit playlistsChanged();
    setLastMessage(QStringLiteral("已加入歌单：%1").arg(n));
    return m_lastMessage;
}

QString PlaybackQueue::saveQueueAsPlaylist(const QString &name)
{
    const auto previous = m_playlists;
    const QString n = name.simplified();
    if (n.isEmpty())
        return QStringLiteral("歌单名不能为空");
    m_playlists[n] = m_queue;
    if (!savePlaylists()) { m_playlists = previous; return m_lastMessage; }
    emit playlistsChanged();
    setLastMessage(QStringLiteral("已保存队列为歌单：%1").arg(n));
    return m_lastMessage;
}

QString PlaybackQueue::loadPlaylist(const QString &name, bool replace)
{
    const QString n = name.simplified();
    if (!m_playlists.contains(n))
        return QStringLiteral("歌单不存在：%1").arg(n);

    QVector<QString> tracks = m_playlists.value(n);
    const auto missing = tracks.removeIf([this](const QString &id) { return rowForTrackId(id) < 0; });
    if (replace) {
        beginResetModel();
        m_queue = tracks;
        m_currentIndex = -1;
        m_resumeIndex = 0;
        endResetModel();
    } else {
        if (tracks.isEmpty()) {
            setLastMessage(QStringLiteral("歌单为空：%1").arg(n));
            return m_lastMessage;
        }
        const int first = m_queue.size();
        beginInsertRows({}, first, first + tracks.size() - 1);
        m_queue += tracks;
        endInsertRows();
    }

    emit queueChanged();
    emit currentIndexChanged();
    setLastMessage(QStringLiteral("已加载歌单：%1 · %2 首，跳过 %3 首已移除曲目").arg(n).arg(tracks.size()).arg(missing));
    return m_lastMessage;
}

QString PlaybackQueue::trackIdForRow(int libraryRow) const
{
    if (!m_library)
        return {};
    const Track *t = m_library->trackAt(libraryRow);
    return t ? t->id : QString();
}

int PlaybackQueue::rowForTrackId(const QString &id) const
{
    return m_library ? m_library->rowOfId(id) : -1;
}

void PlaybackQueue::setCurrentIndex(int index)
{
    if (index < -1 || index >= m_queue.size())
        index = -1;
    if (m_currentIndex == index)
        return;

    const int old = m_currentIndex;
    m_currentIndex = index;
    if (old >= 0)
        emit dataChanged(this->index(old), this->index(old), {ActiveRole});
    if (m_currentIndex >= 0)
        emit dataChanged(this->index(m_currentIndex), this->index(m_currentIndex), {ActiveRole});
    emit currentIndexChanged();
}

void PlaybackQueue::setLastMessage(const QString &message)
{
    if (m_lastMessage == message)
        return;
    m_lastMessage = message;
    emit lastMessageChanged();
}

void PlaybackQueue::loadPlaylists()
{
    QFile f(m_path);
    if (!f.open(QIODevice::ReadOnly))
        return;
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (pe.error == QJsonParseError::NoError && doc.isObject())
        replaceFromJsonObject(doc.object());
}

bool PlaybackQueue::savePlaylists()
{
    QSaveFile file(m_path);
    const auto bytes = QJsonDocument(toJsonObject()).toJson(QJsonDocument::Indented);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        setLastMessage(QStringLiteral("歌单保存失败：%1").arg(file.errorString()));
        return false;
    }
    return true;
}

int PlaybackQueue::enqueueRows(const QVariantList &rows)
{
    QVector<QString> ids;
    for (const auto &row : rows) {
        const QString id = trackIdForRow(row.toInt());
        if (!id.isEmpty()) ids.append(id);
    }
    if (ids.isEmpty()) return 0;
    const int first = m_queue.size();
    beginInsertRows({}, first, first + ids.size() - 1);
    m_queue += ids;
    endInsertRows();
    emit queueChanged();
    return ids.size();
}

QJsonObject PlaybackQueue::toJsonObject() const
{
    QJsonObject root;
    root["schema"] = QStringLiteral("tnuxmusic.playlists.v1");

    QJsonArray arr;
    const QStringList names = playlistNames();
    for (const QString &name : names) {
        QJsonObject obj;
        obj["name"] = name;
        QJsonArray tracks;
        for (const QString &id : m_playlists.value(name))
            tracks.append(id);
        obj["tracks"] = tracks;
        arr.append(obj);
    }
    root["playlists"] = arr;
    return root;
}

void PlaybackQueue::replaceFromJsonObject(const QJsonObject &obj)
{
    m_playlists.clear();
    const QJsonArray arr = obj.value("playlists").toArray();
    for (const auto &v : arr) {
        const QJsonObject p = v.toObject();
        const QString name = p.value("name").toString().simplified();
        if (name.isEmpty())
            continue;
        QVector<QString> ids;
        for (const auto &id : p.value("tracks").toArray()) {
            const QString s = id.toString();
            if (!s.isEmpty())
                ids.push_back(s);
        }
        m_playlists.insert(name, ids);
    }
    emit playlistsChanged();
}
