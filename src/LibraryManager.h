#pragma once

#include "Track.h"

#include <QAbstractListModel>
#include <QJsonObject>

class LibraryManager : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY libraryChanged)
    Q_PROPERTY(QString libraryPath READ libraryPath NOTIFY libraryPathChanged)
    Q_PROPERTY(QString lastMessage READ lastMessage NOTIFY lastMessageChanged)
    Q_PROPERTY(QString searchQuery READ searchQuery WRITE setSearchQuery NOTIFY searchQueryChanged)

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        TitleRole,
        ArtistRole,
        AlbumRole,
        GenreRole,
        YearRole,
        CoverPathRole,
        CoverUrlRole,
        LyricPathRole,
        PrimaryPathRole,
        PrimaryUrlRole,
        QualityCountRole,
        QualitiesTextRole,
        SourceRowRole
    };
    Q_ENUM(Roles)

    explicit LibraryManager(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return m_tracks.size(); }
    QString libraryPath() const { return m_libraryPath; }
    QString lastMessage() const { return m_lastMessage; }
    QString searchQuery() const { return m_searchQuery; }
    void setSearchQuery(const QString &query);
    const QVector<Track> &tracks() const { return m_tracks; }
    const Track *trackAt(int row) const;
    int rowOfId(const QString &id) const;

    Q_INVOKABLE QString loadDefault();
    Q_INVOKABLE QString save();
    Q_INVOKABLE QString scanFolder(const QString &folderUrl);
    Q_INVOKABLE QString importLibrary(const QString &fileUrl);
    Q_INVOKABLE QString mergeLibrary(const QString &fileUrl);
    Q_INVOKABLE QString exportLibrary(const QString &fileUrl) const;
    Q_INVOKABLE QString exportLocalizedZip(const QString &fileUrl) const;
    Q_INVOKABLE QString removeAlbum(const QString &artist, const QString &album);
    Q_INVOKABLE QString clearLibrary();
    Q_INVOKABLE QVariantMap track(int row) const;
    Q_INVOKABLE int rowOfTrackId(const QString &id) const;
    Q_INVOKABLE QString primaryPath(int row) const;
    Q_INVOKABLE QString lyricPath(int row) const;
    Q_INVOKABLE QString coverPath(int row) const;
    Q_INVOKABLE void clear();

    QJsonObject toJsonObject() const;
    bool replaceFromJsonObject(const QJsonObject &obj, QString *error = nullptr, const QString &baseDir = {});
    bool mergeFromJsonObject(const QJsonObject &obj, QString *error = nullptr, const QString &baseDir = {});

signals:
    void libraryChanged();
    void libraryPathChanged();
    void lastMessageChanged();
    void searchQueryChanged();

private:
    QVector<Track> m_tracks;
    QVector<int> m_visibleRows;
    QString m_libraryPath;
    QString m_lastMessage;
    QString m_searchQuery;

    void setLastMessage(const QString &message);
    void rebuildVisibleRows();
    int sourceRowForDisplayRow(int displayRow) const;
    bool readJsonFile(const QString &path, QJsonObject *out, QString *error) const;
    bool readLibrarySource(const QString &path, QJsonObject *out, QString *baseDir, QString *error) const;
    bool writeJsonFile(const QString &path, const QJsonObject &obj, QString *error) const;
    void mergeTrack(const Track &track);
    Track inferTrackFromAudioFile(const QString &path) const;
    void sortTracks();
};
