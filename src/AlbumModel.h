#pragma once

#include <QAbstractListModel>

class LibraryManager;

class AlbumModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY albumsChanged)

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

    Q_INVOKABLE QVariantList tracks(int albumIndex) const;
    Q_INVOKABLE int firstTrackRow(int albumIndex) const;

signals:
    void albumsChanged();

private:
    struct Album {
        QString key;
        QString artist;
        QString title;
        QString coverPath;
        int year = 0;
        QVector<int> rows;
    };

    LibraryManager *m_library = nullptr;
    QVector<Album> m_albums;

    void rebuild();
};

