#pragma once

#include <QAbstractListModel>
#include <QJsonObject>
#include <QStringList>

class LibraryManager;

class PlaybackQueue : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY queueChanged)
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
        ActiveRole
    };
    Q_ENUM(Roles)

    explicit PlaybackQueue(LibraryManager *library, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return m_queue.size(); }
    int currentIndex() const { return m_currentIndex; }
    int currentRow() const;
    QStringList playlistNames() const;
    QString lastMessage() const { return m_lastMessage; }

    Q_INVOKABLE int enqueueRow(int libraryRow);
    Q_INVOKABLE int playNowRow(int libraryRow);
    Q_INVOKABLE int activate(int queueIndex);
    Q_INVOKABLE int next();
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
    void currentIndexChanged();
    void playlistsChanged();
    void lastMessageChanged();

private:
    LibraryManager *m_library = nullptr;
    QVector<QString> m_queue;
    int m_currentIndex = -1;
    QHash<QString, QVector<QString>> m_playlists;
    QString m_path;
    QString m_lastMessage;

    QString trackIdForRow(int libraryRow) const;
    int rowForTrackId(const QString &id) const;
    void setCurrentIndex(int index);
    void setLastMessage(const QString &message);
    void loadPlaylists();
    void savePlaylists();
    QJsonObject toJsonObject() const;
    void replaceFromJsonObject(const QJsonObject &obj);
};

