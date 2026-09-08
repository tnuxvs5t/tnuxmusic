#pragma once

#include <QAbstractListModel>
#include <QJsonObject>
#include <QStringList>

class LibraryManager;

class PlaybackQueue : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY queueChanged)
    Q_PROPERTY(int playbackMode READ playbackMode WRITE setPlaybackMode NOTIFY playbackModeChanged)
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY currentIndexChanged)
    Q_PROPERTY(int currentRow READ currentRow NOTIFY currentIndexChanged)
    Q_PROPERTY(QStringList playlistNames READ playlistNames NOTIFY playlistsChanged)
    Q_PROPERTY(QString lastMessage READ lastMessage NOTIFY lastMessageChanged)

public:
    enum Roles {
        QueueIndexRole = Qt::UserRole + 1,
        LibraryRowRole,
        TitleRole,
        ArtistRole,
        AlbumRole,
        CoverUrlRole,
        ActiveRole,
        TrackIdRole
    };
    Q_ENUM(Roles)

    explicit PlaybackQueue(LibraryManager *library, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    enum PlaybackMode { Sequential, RepeatAll, RepeatOne, Shuffle };
    Q_ENUM(PlaybackMode)
    int playbackMode() const { return m_playbackMode; }
    void setPlaybackMode(int mode);
    int count() const { return m_queue.size(); }
    int currentIndex() const { return m_currentIndex; }
    int currentRow() const;
    QStringList playlistNames() const;
    QString lastMessage() const { return m_lastMessage; }

    Q_INVOKABLE int enqueueRow(int libraryRow);
    Q_INVOKABLE int enqueueRows(const QVariantList &rows);
    Q_INVOKABLE int playNowRow(int libraryRow);
    Q_INVOKABLE int activate(int queueIndex);
    Q_INVOKABLE int next(bool automatic = false);
    Q_INVOKABLE int previous();
    Q_INVOKABLE void removeAt(int queueIndex);
    Q_INVOKABLE void clear();

    Q_INVOKABLE QString createPlaylist(const QString &name);
    Q_INVOKABLE QString deletePlaylist(const QString &name);
    Q_INVOKABLE QString addRowToPlaylist(const QString &name, int libraryRow);
    Q_INVOKABLE QString saveQueueAsPlaylist(const QString &name);
    Q_INVOKABLE QString loadPlaylist(const QString &name, bool replace);

signals:
    void queueChanged();
    void playbackModeChanged();
    void currentIndexChanged();
    void playlistsChanged();
    void lastMessageChanged();

private:
    LibraryManager *m_library = nullptr;
    QVector<QString> m_queue;
    int m_currentIndex = -1;
    int m_resumeIndex = 0;
    int m_playbackMode = RepeatAll;
    QHash<QString, QVector<QString>> m_playlists;
    QString m_path;
    QString m_lastMessage;

    QString trackIdForRow(int libraryRow) const;
    int rowForTrackId(const QString &id) const;
    void setCurrentIndex(int index);
    void setLastMessage(const QString &message);
    void loadPlaylists();
    bool savePlaylists();
    QJsonObject toJsonObject() const;
    void replaceFromJsonObject(const QJsonObject &obj);
};

