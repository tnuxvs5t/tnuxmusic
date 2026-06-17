#pragma once

#include "TlyParser.h"

#include <QAbstractListModel>

class LyricModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString source READ source NOTIFY sourceChanged)
    Q_PROPERTY(int activeIndex READ activeIndex NOTIFY activeIndexChanged)
    Q_PROPERTY(QString title READ title NOTIFY sourceChanged)

public:
    enum Roles {
        StartMsRole = Qt::UserRole + 1,
        EndMsRole,
        TimeTextRole,
        TextRole,
        TranslationRole,
        TagsRole,
        ActiveRole
    };
    Q_ENUM(Roles)

    explicit LyricModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString source() const { return m_source; }
    int activeIndex() const { return m_activeIndex; }
    QString title() const { return m_doc.meta.value("title"); }

    Q_INVOKABLE QString loadFromFile(const QString &pathOrUrl);
    Q_INVOKABLE void clear();
    Q_INVOKABLE void setPosition(qint64 ms);

signals:
    void sourceChanged();
    void activeIndexChanged();

private:
    TlyDocument m_doc;
    QString m_source;
    int m_activeIndex = -1;

    void setActiveIndex(int index);
};

