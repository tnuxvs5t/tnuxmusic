#include "LyricModel.h"

#include "Track.h"

#include <QFileInfo>

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
    case TranslationRole: return QStringList(line.translations.values()).join("\n");
    case TagsRole: return line.tags.join(", ");
    case ActiveRole: return index.row() == m_activeIndex;
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
        {TranslationRole, "translation"},
        {TagsRole, "tags"},
        {ActiveRole, "active"},
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
    endResetModel();
    emit sourceChanged();
    emit activeIndexChanged();

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
    endResetModel();
    emit sourceChanged();
    emit activeIndexChanged();
}

void LyricModel::setPosition(qint64 ms)
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
    setActiveIndex(next);
}

void LyricModel::setActiveIndex(int index)
{
    if (m_activeIndex == index)
        return;

    const int old = m_activeIndex;
    m_activeIndex = index;
    if (old >= 0)
        emit dataChanged(this->index(old), this->index(old), {ActiveRole});
    if (m_activeIndex >= 0)
        emit dataChanged(this->index(m_activeIndex), this->index(m_activeIndex), {ActiveRole});
    emit activeIndexChanged();
}

