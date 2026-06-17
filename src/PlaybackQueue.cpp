#include "PlaybackQueue.h"

#include "LibraryManager.h"
#include "Track.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStandardPaths>

PlaybackQueue::PlaybackQueue(LibraryManager *library, QObject *parent)
    : QAbstractListModel(parent)
    , m_library(library)
{
    const QString root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(root);
    m_path = QDir(root).filePath("playlists.json");
    loadPlaylists();

    if (m_library) {
        connect(m_library, &LibraryManager::libraryChanged, this, [this] {
            if (!m_queue.isEmpty())
                emit dataChanged(index(0), index(m_queue.size() - 1));
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
    const QVariantMap t = m_library->track(row);
    switch (role) {
    case QueueIndexRole: return q;
    case LibraryRowRole: return row;
    case TitleRole: return t.value("title");
    case ArtistRole: return t.value("artist");
    case AlbumRole: return t.value("album");
    case CoverUrlRole: return t.value("coverUrl");
    case ActiveRole: return q == m_currentIndex;
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

int PlaybackQueue::next()
{
    if (m_queue.isEmpty())
        return -1;
    const int nextIndex = (m_currentIndex + 1 + m_queue.size()) % m_queue.size();
    setCurrentIndex(nextIndex);
    return currentRow();
}

int PlaybackQueue::previous()
{
    if (m_queue.isEmpty())
        return -1;
    const int prevIndex = (m_currentIndex <= 0) ? m_queue.size() - 1 : m_currentIndex - 1;
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

    if (m_queue.isEmpty())
        m_currentIndex = -1;
    else if (m_currentIndex >= m_queue.size())
        m_currentIndex = m_queue.size() - 1;
    else if (queueIndex < m_currentIndex)
        --m_currentIndex;

    emit queueChanged();
    emit currentIndexChanged();
}

void PlaybackQueue::clear()
{
    beginResetModel();
    m_queue.clear();
    m_currentIndex = -1;
    endResetModel();
    emit queueChanged();
    emit currentIndexChanged();
    setLastMessage(QStringLiteral("播放队列已清空"));
}

QString PlaybackQueue::createPlaylist(const QString &name)
{
    const QString n = name.simplified();
    if (n.isEmpty())
        return QStringLiteral("歌单名不能为空");
    if (!m_playlists.contains(n))
        m_playlists.insert(n, {});
    savePlaylists();
    emit playlistsChanged();
    setLastMessage(QStringLiteral("已创建歌单：%1").arg(n));
    return m_lastMessage;
}

QString PlaybackQueue::deletePlaylist(const QString &name)
{
    const QString n = name.simplified();
    if (!m_playlists.remove(n))
        return QStringLiteral("歌单不存在：%1").arg(n);
    savePlaylists();
    emit playlistsChanged();
    setLastMessage(QStringLiteral("已删除歌单：%1").arg(n));
    return m_lastMessage;
}

QString PlaybackQueue::addRowToPlaylist(const QString &name, int libraryRow)
{
    const QString n = name.simplified();
    const QString id = trackIdForRow(libraryRow);
    if (n.isEmpty())
        return QStringLiteral("先输入或选择歌单名");
    if (id.isEmpty())
        return QStringLiteral("无法加入歌单：曲目无效");

    auto &tracks = m_playlists[n];
    if (!tracks.contains(id))
        tracks.push_back(id);
    savePlaylists();
    emit playlistsChanged();
    setLastMessage(QStringLiteral("已加入歌单：%1").arg(n));
    return m_lastMessage;
}

QString PlaybackQueue::saveQueueAsPlaylist(const QString &name)
{
    const QString n = name.simplified();
    if (n.isEmpty())
        return QStringLiteral("歌单名不能为空");
    m_playlists[n] = m_queue;
    savePlaylists();
    emit playlistsChanged();
    setLastMessage(QStringLiteral("已保存队列为歌单：%1").arg(n));
    return m_lastMessage;
}

QString PlaybackQueue::loadPlaylist(const QString &name, bool replace)
{
    const QString n = name.simplified();
    if (!m_playlists.contains(n))
        return QStringLiteral("歌单不存在：%1").arg(n);

    const QVector<QString> tracks = m_playlists.value(n);
    if (replace) {
        beginResetModel();
        m_queue = tracks;
        m_currentIndex = m_queue.isEmpty() ? -1 : 0;
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
    setLastMessage(QStringLiteral("已加载歌单：%1").arg(n));
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

void PlaybackQueue::savePlaylists()
{
    QFile f(m_path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    f.write(QJsonDocument(toJsonObject()).toJson(QJsonDocument::Indented));
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
