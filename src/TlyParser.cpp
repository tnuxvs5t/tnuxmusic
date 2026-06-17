#include "TlyParser.h"

#include <QFile>
#include <QRegularExpression>
#include <algorithm>

static QString normalizedMetaKey(QString s)
{
    return s.trimmed().toCaseFolded();
}

static QMap<QString, QString> parseAttrs(const QStringList &parts)
{
    QMap<QString, QString> attrs;
    for (int i = 1; i < parts.size(); ++i) {
        QString p = parts[i].trimmed();
        if (p.isEmpty())
            continue;
        int cut = p.indexOf('=');
        if (cut < 0)
            cut = p.indexOf(':');
        if (cut < 0) {
            attrs.insert(p.toCaseFolded(), QStringLiteral("true"));
        } else {
            attrs.insert(p.left(cut).trimmed().toCaseFolded(), p.mid(cut + 1).trimmed());
        }
    }
    return attrs;
}

static bool isTranslationAttrs(const QMap<QString, QString> &attrs, QString *lang)
{
    if (attrs.contains("tr")) {
        *lang = attrs.value("tr").trimmed().isEmpty() ? QStringLiteral("translation") : attrs.value("tr").trimmed();
        return true;
    }
    if (attrs.value("role").compare("translation", Qt::CaseInsensitive) == 0 ||
        attrs.value("type").compare("translation", Qt::CaseInsensitive) == 0) {
        *lang = attrs.value("lang", QStringLiteral("translation")).trimmed();
        if (lang->isEmpty())
            *lang = QStringLiteral("translation");
        return true;
    }
    return false;
}

TlyDocument TlyParser::parseFile(const QString &path, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error)
            *error = QStringLiteral("无法读取歌词：%1").arg(path);
        return {};
    }
    return parseText(QString::fromUtf8(f.readAll()), error);
}

TlyDocument TlyParser::parseText(const QString &text, QString *error)
{
    TlyDocument doc;
    QMap<qint64, int> mainLineByStart;
    const QStringList rows = text.split(QRegularExpression("[\r\n]"), Qt::SkipEmptyParts);
    static const QRegularExpression timeLineRe(R"(^\s*\[([^\]]+)\]\s*(.*)$)");
    static const QRegularExpression metaRe(R"(^\s*@([^:=\s]+)\s*[:=]\s*(.*)$)");

    for (int lineNo = 0; lineNo < rows.size(); ++lineNo) {
        const QString raw = rows[lineNo];
        const QString row = raw.trimmed();
        if (row.isEmpty() || row.startsWith('#'))
            continue;

        const auto mm = metaRe.match(row);
        if (mm.hasMatch()) {
            doc.meta.insert(normalizedMetaKey(mm.captured(1)), mm.captured(2).trimmed());
            continue;
        }

        const auto m = timeLineRe.match(raw);
        if (!m.hasMatch())
            continue;

        QString inside = m.captured(1);
        const QString body = m.captured(2).trimmed();
        const QStringList parts = inside.split('|');
        QString timePart = parts.value(0).trimmed();
        const auto attrs = parseAttrs(parts);

        QString startText = timePart;
        QString endText;
        int arrow = timePart.indexOf("-->");
        if (arrow < 0)
            arrow = timePart.indexOf("->");
        if (arrow >= 0) {
            startText = timePart.left(arrow).trimmed();
            endText = timePart.mid(arrow + (timePart.mid(arrow).startsWith("-->") ? 3 : 2)).trimmed();
        }

        bool okStart = false;
        bool okEnd = true;
        const qint64 start = parseTimeMs(startText, &okStart);
        qint64 end = -1;
        if (!endText.isEmpty())
            end = parseTimeMs(endText, &okEnd);

        if (!okStart || !okEnd) {
            if (error)
                *error = QStringLiteral("TLY 第 %1 行时间格式错误：%2").arg(lineNo + 1).arg(raw);
            continue;
        }

        QString lang;
        if (isTranslationAttrs(attrs, &lang)) {
            if (!mainLineByStart.contains(start)) {
                TlyLine base;
                base.startMs = start;
                base.endMs = end;
                doc.lines.push_back(base);
                mainLineByStart.insert(start, doc.lines.size() - 1);
            }
            doc.lines[mainLineByStart.value(start)].translations.insert(lang, body);
            continue;
        }

        TlyLine line;
        line.startMs = start;
        line.endMs = end;
        line.text = body;
        for (auto it = attrs.begin(); it != attrs.end(); ++it)
            line.tags << (it.value() == "true" ? it.key() : it.key() + "=" + it.value());
        doc.lines.push_back(line);
        mainLineByStart.insert(start, doc.lines.size() - 1);
    }

    std::sort(doc.lines.begin(), doc.lines.end(), [](const TlyLine &a, const TlyLine &b) {
        if (a.startMs != b.startMs)
            return a.startMs < b.startMs;
        return a.text < b.text;
    });

    for (int i = 0; i < doc.lines.size(); ++i) {
        if (doc.lines[i].endMs < 0 && i + 1 < doc.lines.size())
            doc.lines[i].endMs = std::max<qint64>(doc.lines[i].startMs, doc.lines[i + 1].startMs - 1);
    }

    if (error && error->isEmpty())
        error->clear();
    return doc;
}

qint64 TlyParser::parseTimeMs(const QString &text, bool *ok)
{
    QString s = text.trimmed();
    s.replace(',', '.');
    const QStringList parts = s.split(':');
    if (parts.isEmpty()) {
        if (ok)
            *ok = false;
        return 0;
    }

    bool localOk = true;
    double seconds = parts.last().toDouble(&localOk);
    if (!localOk) {
        if (ok)
            *ok = false;
        return 0;
    }

    qint64 totalSeconds = static_cast<qint64>(seconds);
    qint64 ms = static_cast<qint64>((seconds - totalSeconds) * 1000.0 + 0.5);
    qint64 multiplier = 60;
    for (int i = parts.size() - 2; i >= 0; --i) {
        bool partOk = false;
        const qint64 v = parts[i].trimmed().toLongLong(&partOk);
        if (!partOk) {
            if (ok)
                *ok = false;
            return 0;
        }
        totalSeconds += v * multiplier;
        multiplier *= 60;
    }

    if (ok)
        *ok = true;
    return totalSeconds * 1000 + ms;
}

QString TlyParser::formatTime(qint64 ms)
{
    if (ms < 0)
        ms = 0;
    const qint64 total = ms / 1000;
    const qint64 msec = ms % 1000;
    const qint64 sec = total % 60;
    const qint64 min = (total / 60) % 60;
    const qint64 hour = total / 3600;
    if (hour > 0)
        return QStringLiteral("%1:%2:%3.%4")
            .arg(hour)
            .arg(min, 2, 10, QLatin1Char('0'))
            .arg(sec, 2, 10, QLatin1Char('0'))
            .arg(msec, 3, 10, QLatin1Char('0'));
    return QStringLiteral("%1:%2.%3")
        .arg(min, 2, 10, QLatin1Char('0'))
        .arg(sec, 2, 10, QLatin1Char('0'))
        .arg(msec, 3, 10, QLatin1Char('0'));
}

