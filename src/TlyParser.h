#pragma once

#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

struct TlyLine {
    qint64 startMs = 0;
    qint64 endMs = -1;
    QString text;
    QMap<QString, QString> translations;
    QStringList tags;
};

struct TlyDocument {
    QMap<QString, QString> meta;
    QVector<TlyLine> lines;
};

class TlyParser {
public:
    static TlyDocument parseFile(const QString &path, QString *error = nullptr);
    static TlyDocument parseText(const QString &text, QString *error = nullptr);
    static qint64 parseTimeMs(const QString &text, bool *ok = nullptr);
    static QString formatTime(qint64 ms);
};

