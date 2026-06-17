#include "LyricModel.h"

#include "Track.h"

#include <QFileInfo>
#include <QSet>
#include <algorithm>
#include <limits>

LyricModel::LyricModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int LyricModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_doc.lines.size();
}

QVariant LyricModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_doc.lines.size())
        return {};

    const TlyLine &line = m_doc.lines[index.row()];
    switch (role) {
    case StartMsRole: return line.startMs;
    case EndMsRole: return line.endMs;
    case TimeTextRole: return TlyParser::formatTime(line.startMs);
    case TextRole: return line.text;
    case RichTextRole: return richTextFor(line);
    case TranslationRole: return QStringList(line.translations.values()).join("\n");
    case TagsRole: return line.tags.join(", ");
    case ActiveRole: return index.row() == m_activeIndex;
    case ActiveWordRole: return activeWordIndex(index.row());
    case ProgressRole: {
        if (index.row() != m_activeIndex || line.endMs <= line.startMs)
            return 0.0;
        return std::clamp(double(m_positionMs - line.startMs) / double(line.endMs - line.startMs), 0.0, 1.0);
    }
    default: return {};
    }
}

QHash<int, QByteArray> LyricModel::roleNames() const
{
    return {
        {StartMsRole, "startMs"},
        {EndMsRole, "endMs"},
        {TimeTextRole, "timeText"},
        {TextRole, "text"},
        {RichTextRole, "richText"},
        {TranslationRole, "translation"},
        {TagsRole, "tags"},
        {ActiveRole, "active"},
        {ActiveWordRole, "activeWord"},
        {ProgressRole, "progress"},
    };
}

QString LyricModel::loadFromFile(const QString &pathOrUrl)
{
    const QString path = canonicalLocalPath(pathOrUrl);
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        clear();
        return QStringLiteral("没有找到 .tly 歌词");
    }

    QString error;
    TlyDocument next = TlyParser::parseFile(path, &error);

    beginResetModel();
    m_doc = next;
    m_source = path;
    m_activeIndex = -1;
    m_positionMs = 0;
    endResetModel();
    emit sourceChanged();
    emit activeIndexChanged();
    emit positionChanged();

    if (!error.isEmpty())
        return error;
    return QStringLiteral("已加载歌词：%1 行").arg(m_doc.lines.size());
}

void LyricModel::clear()
{
    beginResetModel();
    m_doc = {};
    m_source.clear();
    m_activeIndex = -1;
    m_positionMs = 0;
    endResetModel();
    emit sourceChanged();
    emit activeIndexChanged();
    emit positionChanged();
}

void LyricModel::setPosition(qint64 ms)
{
    const int oldLine = m_activeIndex;
    const int oldWord = activeWordIndex(oldLine);
    m_positionMs = ms;
    emit positionChanged();

    const int nextLine = findActiveIndex(ms);
    const int nextWord = nextLine >= 0 ? activeWordIndex(nextLine) : -1;

    if (oldLine == nextLine && oldWord == nextWord) {
        if (nextLine >= 0)
            emit dataChanged(index(nextLine), index(nextLine), {ProgressRole});
        return;
    }

    m_activeIndex = nextLine;
    QSet<int> dirty;
    if (oldLine >= 0)
        dirty.insert(oldLine);
    if (nextLine >= 0)
        dirty.insert(nextLine);

    for (int row : dirty) {
        emit dataChanged(index(row), index(row),
                         {ActiveRole, RichTextRole, ActiveWordRole, ProgressRole});
    }

    if (oldLine != nextLine)
        emit activeIndexChanged();
}

int LyricModel::findActiveIndex(qint64 ms) const
{
    int next = -1;
    for (int i = 0; i < m_doc.lines.size(); ++i) {
        const auto &line = m_doc.lines[i];
        const qint64 end = line.endMs >= 0 ? line.endMs : std::numeric_limits<qint64>::max();
        if (ms >= line.startMs && ms <= end)
            next = i;
        if (line.startMs > ms)
            break;
    }
    return next;
}

int LyricModel::activeWordIndex(int lineIndex) const
{
    if (lineIndex < 0 || lineIndex >= m_doc.lines.size())
        return -1;

    const auto &words = m_doc.lines[lineIndex].words;
    for (int i = 0; i < words.size(); ++i) {
        const qint64 end = words[i].endMs >= 0 ? words[i].endMs : std::numeric_limits<qint64>::max();
        if (m_positionMs >= words[i].startMs && m_positionMs <= end)
            return i;
    }
    return -1;
}

QString LyricModel::richTextFor(const TlyLine &line) const
{
    if (line.words.isEmpty())
        return line.text.toHtmlEscaped();

    QString html;
    for (const auto &word : line.words) {
        const qint64 end = word.endMs >= 0 ? word.endMs : std::numeric_limits<qint64>::max();
        const bool active = m_positionMs >= word.startMs && m_positionMs <= end;
        const bool past = m_positionMs > end;
        const QString color = active ? QStringLiteral("#ffffff")
            : (past ? QStringLiteral("#48d1cc") : QStringLiteral("#d8e1e3"));
        const QString weight = active ? QStringLiteral("700") : QStringLiteral("400");
        const QString bg = active ? QStringLiteral("background-color:rgba(72,209,204,0.25); border-radius:6px;") : QString();
        html += QStringLiteral("<span style=\"color:%1; font-weight:%2; %3\">%4</span>")
                    .arg(color, weight, bg, word.text.toHtmlEscaped());
    }
    return html;
}
