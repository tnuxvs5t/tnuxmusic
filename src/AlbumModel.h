#pragma once

#include <QAbstractListModel>
#include <QSet>

class LibraryManager;

class AlbumModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY albumsChanged)
    Q_PROPERTY(int sortMode READ sortMode WRITE setSortMode NOTIFY sortModeChanged)
    Q_PROPERTY(QString searchQuery READ searchQuery WRITE setSearchQuery NOTIFY searchQueryChanged)
    Q_PROPERTY(bool autoMergeAlbums READ autoMergeAlbums WRITE setAutoMergeAlbums NOTIFY autoMergeAlbumsChanged)

public:
    enum Roles {
        KeyRole = Qt::UserRole + 1,
        ArtistRole,
        AlbumRole,
        CountRole,
        CoverUrlRole,
        YearRole,
        SubtitleRole
    };
    Q_ENUM(Roles)

    explicit AlbumModel(LibraryManager *library, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return m_albums.size(); }
    int sortMode() const { return m_sortMode; }
    void setSortMode(int mode);
    QString searchQuery() const { return m_searchQuery; }
    void setSearchQuery(const QString &query);
    Q_INVOKABLE int indexOfKey(const QString &key) const;
    Q_INVOKABLE QVariantMap info(const QString &key) const;
    Q_INVOKABLE QVariantList choices(const QString &exceptKey = {}, const QString &query = {}) const;
    Q_INVOKABLE QVariantList tracksForKey(const QString &key) const;
    Q_INVOKABLE QString moveTracks(const QString &sourceKey, const QStringList &ids, const QString &targetKey);
    Q_INVOKABLE QString splitTracks(const QString &sourceKey, const QStringList &ids, const QString &title, const QString &artist, int year);
    Q_INVOKABLE QString editAlbum(const QString &key, const QString &title, const QString &artist, int year);
    Q_INVOKABLE QString mergeAlbums(const QString &sourceKey, const QString &targetKey);
    Q_INVOKABLE QString removeAlbum(const QString &key);
    bool autoMergeAlbums() const { return m_autoMergeAlbums; }
    void setAutoMergeAlbums(bool enabled);

    Q_INVOKABLE QVariantList tracks(int albumIndex) const;
    Q_INVOKABLE int firstTrackRow(int albumIndex) const;

signals:
    void albumsChanged();
    void sortModeChanged();
    void searchQueryChanged();
    void autoMergeAlbumsChanged();

private:
    struct Album {
        QString key;
        QString artist;
        QString title;
        QString coverPath;
        int year = 0;
        QVector<int> rows;
        QSet<QString> artists;
        QString searchText;
    };

    LibraryManager *m_library = nullptr;
    QVector<Album> m_albums;
    bool m_autoMergeAlbums = false;
    QString m_searchQuery;
    int m_sortMode = 0;
    QVector<int> m_visibleAlbums;
    QHash<QString, int> m_byKey;
    QStringList trackIds(const QString &key) const;
    void filter();

    void rebuild();
};
