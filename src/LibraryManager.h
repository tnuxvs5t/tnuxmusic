#pragma once

#include "Track.h"

#include <QAbstractListModel>
#include <QHash>
#include <QJsonObject>
#include <QFutureWatcher>
#include <QSet>
#include <atomic>
#include <memory>

class LibraryManager : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY libraryChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool canCancel READ canCancel NOTIFY busyChanged)
    Q_PROPERTY(bool canUndoEdit READ canUndoEdit NOTIFY undoAvailableChanged)
    Q_PROPERTY(QString operation READ operation NOTIFY busyChanged)
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

    explicit LibraryManager(QObject *parent = nullptr, const QString &libraryFile = {});
    bool operationSucceeded() const { return m_operationSucceeded; }
    ~LibraryManager() override;
    bool busy() const { return m_busy; }
    bool canCancel() const { return m_busy && m_cancelState && m_cancelState->load() == 0; }
    Q_INVOKABLE void cancelOperation();
    bool canUndoEdit() const { return m_hasUndo; }
    Q_INVOKABLE QString undoLastEdit();
    QString operation() const { return m_operation; }
    Q_INVOKABLE bool startOperation(const QString &kind, const QString &url = {});
    QString updateAlbum(const QStringList &ids, const QString &title, const QString &artist, int year, const QString &albumId);
    QString removeTracks(const QStringList &ids);

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
    Q_INVOKABLE QString refreshAlbumMetadata();
    Q_INVOKABLE QString scanFolder(const QString &folderUrl);
    Q_INVOKABLE QString importLibrary(const QString &fileUrl);
    Q_INVOKABLE QString mergeLibrary(const QString &fileUrl);
    Q_INVOKABLE QString exportLibrary(const QString &fileUrl);
    Q_INVOKABLE QString exportLocalizedZip(const QString &fileUrl);
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
    bool replaceAndSave(const QJsonObject &obj, QString *error = nullptr);
    bool mergeFromJsonObject(const QJsonObject &obj, QString *error = nullptr, const QString &baseDir = {});

signals:
    void busyChanged();
    void undoAvailableChanged();
    void operationFinished(bool success, const QString &message);
    void libraryChanged();
    void libraryPathChanged();
    void lastMessageChanged();
    void searchQueryChanged();

private:
    struct JobResult { QVector<Track> tracks; QString message; bool success = false; bool changed = false; };
    QFutureWatcher<JobResult> m_job;
    bool m_busy = false;
    bool m_preparing = false;
    // Shared job state: 0 preparing, 1 cancellation accepted, 2 commit started.
    std::shared_ptr<std::atomic_int> m_cancelState;
    bool cancelled() const { return m_cancelState && m_cancelState->load() == 1; }
    QString performOperation(const QString &kind, const QString &url);
    bool m_hasUndo = false;
    QVector<Track> m_undoTracks;
    bool m_operationSucceeded = false;
    QString m_operation;
    QHash<QString, int> m_trackIndexById;
    QHash<QString, int> m_trackIndexByPath;
    bool persist(QString *error);
    bool commitTracks(QVector<Track> tracks, QString *error);
    QVector<Track> m_tracks;
    QHash<QString, int> m_trackIndexByKey;
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
    void rebuildTrackIndex();
    void mergeTrack(const Track &track);
    Track inferTrackFromAudioFile(const QString &path) const;
    void sortTracks();
};
