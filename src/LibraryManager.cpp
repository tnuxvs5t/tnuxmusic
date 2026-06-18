#include "LibraryManager.h"

#include "MetadataReader.h"
#include "NcmImportService.h"

#include <QDir>
#include <QDirIterator>
#include <QCoreApplication>
#include <QCollator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QHash>
#include <QIODevice>
#include <QCryptographicHash>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QTextStream>
#include <algorithm>
#include <array>
#include <limits>

static const QSet<QString> kAudioExt = {
    "mp3", "flac", "wav", "m4a", "aac", "ogg", "opus", "wma", "aiff", "alac"
};

static const QSet<QString> kEncryptedExt = {
    "ncm"
};

static const QStringList kCoverNames = {
    "cover", "folder", "front", "album", "artwork"
};

static const QStringList kImageExt = {
    "jpg", "jpeg", "png", "webp", "bmp"
};

struct AlbumSidecar {
    bool valid = false;
    QString album;
    QString artist;
    QString genre;
    QString coverPath;
    int year = 0;
    QJsonObject track;
};

struct ZipEntryInfo {
    QString name;
    quint32 crc = 0;
    quint32 size = 0;
    quint32 localHeaderOffset = 0;
};

constexpr quint16 kZipDosTime = 0;
constexpr quint16 kZipDosDate = (1 << 5) | 1; // 1980-01-01

#ifndef TNUXMUSIC_SOURCE_DIR
#define TNUXMUSIC_SOURCE_DIR ""
#endif

static QString cleanTitle(QString s)
{
    s.replace('_', ' ');
    static const QRegularExpression qualitySuffix(
        R"(\s*[\[(](flac|mp3|lossless|hi[- ]?res|24bit|16bit|320k|256k|192k|128k|hq|sq)[\])]\s*$)",
        QRegularExpression::CaseInsensitiveOption);
    s.remove(qualitySuffix);
    return s.simplified();
}

static QString textValue(const QJsonObject &obj, const QString &key)
{
    return obj.value(key).toString().trimmed();
}

static int intValue(const QJsonObject &obj, const QString &key, int fallback = 0)
{
    const QJsonValue v = obj.value(key);
    if (v.isDouble())
        return v.toInt(fallback);
    bool ok = false;
    const int x = v.toString().toInt(&ok);
    return ok ? x : fallback;
}

static quint32 crc32Bytes(const QByteArray &data, quint32 crc = 0xffffffffu)
{
    static const std::array<quint32, 256> table = [] {
        std::array<quint32, 256> t {};
        for (quint32 i = 0; i < 256; ++i) {
            quint32 c = i;
            for (int j = 0; j < 8; ++j)
                c = (c & 1) ? (0xedb88320u ^ (c >> 1)) : (c >> 1);
            t[i] = c;
        }
        return t;
    }();

    for (uchar b : data)
        crc = table[(crc ^ b) & 0xff] ^ (crc >> 8);
    return crc;
}

static void writeLe16(QFile &file, quint16 value)
{
    char bytes[2] = {
        char(value & 0xff),
        char((value >> 8) & 0xff),
    };
    file.write(bytes, 2);
}

static void writeLe32(QFile &file, quint32 value)
{
    char bytes[4] = {
        char(value & 0xff),
        char((value >> 8) & 0xff),
        char((value >> 16) & 0xff),
        char((value >> 24) & 0xff),
    };
    file.write(bytes, 4);
}

static quint16 readZipLe16(const QByteArray &bytes, int offset)
{
    const auto *p = reinterpret_cast<const uchar *>(bytes.constData() + offset);
    return quint16(p[0]) | (quint16(p[1]) << 8);
}

static quint32 readZipLe32(const QByteArray &bytes, int offset)
{
    const auto *p = reinterpret_cast<const uchar *>(bytes.constData() + offset);
    return quint32(p[0]) | (quint32(p[1]) << 8) | (quint32(p[2]) << 16) | (quint32(p[3]) << 24);
}

static QString zipSafeName(QString name)
{
    name.replace('\\', '/');
    while (name.startsWith('/'))
        name.remove(0, 1);
    const QStringList parts = name.split('/', Qt::SkipEmptyParts);
    QStringList safe;
    safe.reserve(parts.size());
    for (QString part : parts) {
        part = part.trimmed();
        if (part.isEmpty() || part == "." || part == "..")
            continue;
        part.replace(QRegularExpression(R"([<>:"|?*\x00-\x1f])"), "_");
        safe << part;
    }
    return safe.join('/');
}

static QString imageExtForData(const QByteArray &data, const QString &mimeType = {})
{
    const QString mime = mimeType.trimmed().toLower();
    if (mime == "image/jpeg" || mime == "image/jpg")
        return QStringLiteral("jpg");
    if (mime == "image/png")
        return QStringLiteral("png");
    if (mime == "image/webp")
        return QStringLiteral("webp");
    if (mime == "image/bmp")
        return QStringLiteral("bmp");
    if (data.startsWith(QByteArray("\xff\xd8", 2)))
        return QStringLiteral("jpg");
    if (data.startsWith(QByteArray("\x89PNG\r\n\x1a\n", 8)))
        return QStringLiteral("png");
    if (data.startsWith("RIFF") && data.mid(8, 4) == "WEBP")
        return QStringLiteral("webp");
    if (data.startsWith("BM"))
        return QStringLiteral("bmp");
    return QStringLiteral("jpg");
}

static QString writeEmbeddedCoverSidecar(const QFileInfo &audio, const AudioMetadata &meta)
{
    if (meta.coverData.isEmpty())
        return {};

    const QString ext = imageExtForData(meta.coverData.left(16), meta.coverMimeType);
    const QString path = audio.dir().filePath(audio.completeBaseName() + QStringLiteral(".cover.") + ext);
    const QFileInfo coverInfo(path);
    if (coverInfo.exists() && coverInfo.size() > 0 && coverInfo.lastModified() >= audio.lastModified())
        return canonicalLocalPath(path);

    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly))
        return {};
    if (out.write(meta.coverData) != meta.coverData.size()) {
        out.cancelWriting();
        return {};
    }
    if (!out.commit())
        return {};
    return canonicalLocalPath(path);
}

static QString libraryImportCacheRoot()
{
    const QString root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return QDir(root).filePath("import-cache");
}

static QString zipCacheDirFor(const QString &archivePath)
{
    const QString base = libraryImportCacheRoot();
    QDir().mkpath(base);

    const QFileInfo info(archivePath);
    const QByteArray key = info.absoluteFilePath().toUtf8() + "|" + QByteArray::number(info.size()) + "|"
        + QByteArray::number(info.lastModified().toMSecsSinceEpoch());
    const QByteArray digest = QCryptographicHash::hash(key, QCryptographicHash::Sha1).toHex();
    return QDir(base).filePath(QString::fromLatin1(digest));
}

static bool extractZipToDir(const QString &zipPath, const QString &destDir, QString *error)
{
    QDir().mkpath(destDir);

    QFile zip(zipPath);
    if (!zip.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("无法读取 ZIP：%1").arg(zipPath);
        return false;
    }

    while (!zip.atEnd()) {
        const QByteArray header = zip.read(30);
        if (header.isEmpty())
            break;
        if (header.size() < 30 || readZipLe32(header, 0) != 0x04034b50)
            break;

        const quint16 method = readZipLe16(header, 8);
        const quint32 compressedSize = readZipLe32(header, 18);
        const quint32 uncompressedSize = readZipLe32(header, 22);
        const quint16 nameLen = readZipLe16(header, 26);
        const quint16 extraLen = readZipLe16(header, 28);
        const QByteArray nameBytes = zip.read(nameLen);
        if (nameBytes.size() != nameLen || !zip.seek(zip.pos() + extraLen)) {
            if (error)
                *error = QStringLiteral("ZIP 文件损坏：%1").arg(zipPath);
            return false;
        }

        QString name = QString::fromUtf8(nameBytes);
        name.replace('\\', '/');
        name = QDir::cleanPath(name);
        if (name.isEmpty() || name.startsWith("../") || name.contains("/../") || name == "..") {
            if (!zip.seek(zip.pos() + compressedSize))
                return false;
            continue;
        }

        if (name.endsWith('/')) {
            QDir().mkpath(QDir(destDir).filePath(name));
            continue;
        }
        if (method != 0) {
            if (error)
                *error = QStringLiteral("ZIP 条目使用压缩算法 %1，当前内置导入器只支持本应用导出的 store ZIP：%2")
                             .arg(method)
                             .arg(name);
            return false;
        }
        if (compressedSize != uncompressedSize) {
            if (error)
                *error = QStringLiteral("ZIP 条目大小异常：%1").arg(name);
            return false;
        }

        const QFileInfo outInfo(QDir(destDir).filePath(name));
        if (!outInfo.absoluteDir().exists())
            QDir().mkpath(outInfo.absolutePath());
        QFile out(outInfo.absoluteFilePath());
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            if (error)
                *error = QStringLiteral("无法写入解包文件：%1").arg(name);
            return false;
        }

        quint32 remaining = compressedSize;
        while (remaining > 0) {
            const QByteArray chunk = zip.read(qMin<quint32>(remaining, 128 * 1024));
            if (chunk.isEmpty()) {
                if (error)
                    *error = QStringLiteral("ZIP 条目截断：%1").arg(name);
                return false;
            }
            if (out.write(chunk) != chunk.size()) {
                if (error)
                    *error = QStringLiteral("无法写入解包文件：%1").arg(name);
                return false;
            }
            remaining -= chunk.size();
        }
    }
    return true;
}

static bool readLibrarySourceInternal(const QString &path, QJsonObject *out, QString *baseDir, QString *error)
{
    const QFileInfo info(path);
    if (!info.exists()) {
        if (error)
            *error = QStringLiteral("文件不存在：%1").arg(path);
        return false;
    }

    if (info.suffix().toLower() == "zip") {
        const QString destDir = zipCacheDirFor(info.absoluteFilePath());
        const QString marker = QDir(destDir).filePath(".unzipped");
        if (!QFileInfo::exists(marker)) {
            if (!extractZipToDir(info.absoluteFilePath(), destDir, error))
                return false;
            QFile markerFile(marker);
            if (markerFile.open(QIODevice::WriteOnly | QIODevice::Truncate))
                markerFile.write("ok");
        }

        QFile f(QDir(destDir).filePath("library.json"));
        if (!f.open(QIODevice::ReadOnly)) {
            if (error)
                *error = QStringLiteral("ZIP 中缺少 library.json：%1").arg(path);
            return false;
        }
        QJsonParseError pe;
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
        if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
            if (error)
                *error = QStringLiteral("ZIP 曲库 JSON 错误：%1").arg(pe.errorString());
            return false;
        }
        *out = doc.object();
        if (baseDir)
            *baseDir = destDir;
        return true;
    }

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("无法读取 JSON：%1").arg(path);
        return false;
    }
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error)
            *error = QStringLiteral("JSON 格式错误：%1 (%2)").arg(path, pe.errorString());
        return false;
    }
    *out = doc.object();
    if (baseDir)
        *baseDir = info.absolutePath();
    return true;
}

static QString uniqueZipName(const QString &preferred, QSet<QString> *used)
{
    QString name = zipSafeName(preferred);
    if (name.isEmpty())
        name = QStringLiteral("file");

    const QFileInfo info(name);
    const QString dir = info.path() == "." ? QString() : info.path() + "/";
    const QString stem = info.completeBaseName();
    const QString suffix = info.suffix();

    QString candidate = name;
    int n = 2;
    while (used->contains(candidate)) {
        candidate = dir + stem + QStringLiteral("-%1").arg(n++);
        if (!suffix.isEmpty())
            candidate += "." + suffix;
    }
    used->insert(candidate);
    return candidate;
}

static bool addZipEntry(QFile &zip, const QString &entryName, const QByteArray &data, QVector<ZipEntryInfo> *entries)
{
    const QByteArray nameUtf8 = entryName.toUtf8();
    if (nameUtf8.isEmpty() || nameUtf8.size() > 65535 || data.size() > std::numeric_limits<quint32>::max()
        || zip.pos() > std::numeric_limits<quint32>::max()) {
        return false;
    }

    const quint32 crc = ~crc32Bytes(data);
    const quint32 size = quint32(data.size());
    const quint32 offset = quint32(zip.pos());

    writeLe32(zip, 0x04034b50);
    writeLe16(zip, 20);
    writeLe16(zip, 0x0800); // UTF-8 file names.
    writeLe16(zip, 0);      // stored
    writeLe16(zip, kZipDosTime);
    writeLe16(zip, kZipDosDate);
    writeLe32(zip, crc);
    writeLe32(zip, size);
    writeLe32(zip, size);
    writeLe16(zip, quint16(nameUtf8.size()));
    writeLe16(zip, 0);
    zip.write(nameUtf8);
    zip.write(data);

    entries->push_back({entryName, crc, size, offset});
    return zip.error() == QFile::NoError;
}

static bool addZipFile(QFile &zip,
                       const QString &sourcePath,
                       const QString &entryName,
                       QVector<ZipEntryInfo> *entries,
                       QString *error)
{
    QFile in(sourcePath);
    if (!in.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("无法读取本地化资源：%1").arg(sourcePath);
        return false;
    }

    if (in.size() < 0 || in.size() > std::numeric_limits<quint32>::max()
        || zip.pos() > std::numeric_limits<quint32>::max()) {
        if (error)
            *error = QStringLiteral("资源过大，当前导出器不支持 ZIP64：%1").arg(sourcePath);
        return false;
    }

    quint32 crcState = 0xffffffffu;
    while (!in.atEnd()) {
        const QByteArray chunk = in.read(128 * 1024);
        if (chunk.isEmpty() && in.error() != QFile::NoError) {
            if (error)
                *error = QStringLiteral("读取本地化资源失败：%1").arg(sourcePath);
            return false;
        }
        crcState = crc32Bytes(chunk, crcState);
    }

    if (!in.seek(0)) {
        if (error)
            *error = QStringLiteral("无法重读本地化资源：%1").arg(sourcePath);
        return false;
    }

    const QByteArray nameUtf8 = entryName.toUtf8();
    if (nameUtf8.isEmpty() || nameUtf8.size() > 65535) {
        if (error)
            *error = QStringLiteral("无法写入 ZIP 条目：%1").arg(entryName);
        return false;
    }

    const quint32 crc = ~crcState;
    const quint32 size = quint32(in.size());
    const quint32 offset = quint32(zip.pos());

    writeLe32(zip, 0x04034b50);
    writeLe16(zip, 20);
    writeLe16(zip, 0x0800);
    writeLe16(zip, 0);
    writeLe16(zip, kZipDosTime);
    writeLe16(zip, kZipDosDate);
    writeLe32(zip, crc);
    writeLe32(zip, size);
    writeLe32(zip, size);
    writeLe16(zip, quint16(nameUtf8.size()));
    writeLe16(zip, 0);
    zip.write(nameUtf8);

    while (!in.atEnd()) {
        const QByteArray chunk = in.read(128 * 1024);
        if (chunk.isEmpty() && in.error() != QFile::NoError) {
            if (error)
                *error = QStringLiteral("读取本地化资源失败：%1").arg(sourcePath);
            return false;
        }
        if (zip.write(chunk) != chunk.size()) {
            if (error)
                *error = QStringLiteral("无法写入 ZIP 条目：%1").arg(entryName);
            return false;
        }
    }

    entries->push_back({entryName, crc, size, offset});
    return true;
}

static bool finishZip(QFile &zip, const QVector<ZipEntryInfo> &entries, QString *error)
{
    if (zip.pos() > std::numeric_limits<quint32>::max()) {
        if (error)
            *error = QStringLiteral("ZIP 超过 4GiB，当前导出器不支持 ZIP64");
        return false;
    }
    const quint32 centralOffset = quint32(zip.pos());

    for (const ZipEntryInfo &entry : entries) {
        const QByteArray nameUtf8 = entry.name.toUtf8();
        writeLe32(zip, 0x02014b50);
        writeLe16(zip, 20);
        writeLe16(zip, 20);
        writeLe16(zip, 0x0800);
        writeLe16(zip, 0);
        writeLe16(zip, kZipDosTime);
        writeLe16(zip, kZipDosDate);
        writeLe32(zip, entry.crc);
        writeLe32(zip, entry.size);
        writeLe32(zip, entry.size);
        writeLe16(zip, quint16(nameUtf8.size()));
        writeLe16(zip, 0);
        writeLe16(zip, 0);
        writeLe16(zip, 0);
        writeLe16(zip, 0);
        writeLe32(zip, 0);
        writeLe32(zip, entry.localHeaderOffset);
        zip.write(nameUtf8);
    }

    if (zip.pos() > std::numeric_limits<quint32>::max() || entries.size() > 65535) {
        if (error)
            *error = QStringLiteral("ZIP 条目过多或过大，当前导出器不支持 ZIP64");
        return false;
    }

    const quint32 centralSize = quint32(zip.pos()) - centralOffset;
    writeLe32(zip, 0x06054b50);
    writeLe16(zip, 0);
    writeLe16(zip, 0);
    writeLe16(zip, quint16(entries.size()));
    writeLe16(zip, quint16(entries.size()));
    writeLe32(zip, centralSize);
    writeLe32(zip, centralOffset);
    writeLe16(zip, 0);
    return zip.error() == QFile::NoError;
}

static QString qualityLabelFor(const QFileInfo &info)
{
    const QString ext = info.suffix().toLower();
    const QString name = info.completeBaseName().toLower();
    if (ext == "flac" || ext == "alac" || ext == "wav" || ext == "aiff")
        return QStringLiteral("Lossless");
    if (name.contains("320"))
        return QStringLiteral("MP3 320k");
    if (name.contains("256"))
        return QStringLiteral("AAC/MP3 256k");
    if (name.contains("128"))
        return QStringLiteral("MP3 128k");
    return ext.toUpper();
}

struct LrcLine {
    qint64 startMs = 0;
    QString text;
};

static qint64 parseLrcTimeMs(QString s, bool *ok = nullptr)
{
    s = s.trimmed();
    s.replace(',', '.');
    const QStringList parts = s.split(':');
    if (parts.size() < 2) {
        if (ok)
            *ok = false;
        return 0;
    }

    bool secOk = false;
    const double secDouble = parts.last().toDouble(&secOk);
    if (!secOk) {
        if (ok)
            *ok = false;
        return 0;
    }

    qint64 totalMs = qint64(secDouble * 1000.0 + 0.5);
    qint64 mul = 60 * 1000;
    for (int i = parts.size() - 2; i >= 0; --i) {
        bool partOk = false;
        const qint64 v = parts[i].toLongLong(&partOk);
        if (!partOk) {
            if (ok)
                *ok = false;
            return 0;
        }
        totalMs += v * mul;
        mul *= 60;
    }

    if (ok)
        *ok = true;
    return totalMs;
}

static QString formatTlyTime(qint64 ms)
{
    if (ms < 0)
        ms = 0;
    const qint64 h = ms / 3600000;
    ms %= 3600000;
    const qint64 m = ms / 60000;
    ms %= 60000;
    const qint64 s = ms / 1000;
    const qint64 z = ms % 1000;
    if (h > 0) {
        return QStringLiteral("%1:%2:%3.%4")
            .arg(h)
            .arg(m, 2, 10, QLatin1Char('0'))
            .arg(s, 2, 10, QLatin1Char('0'))
            .arg(z, 3, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1:%2.%3")
        .arg(m, 2, 10, QLatin1Char('0'))
        .arg(s, 2, 10, QLatin1Char('0'))
        .arg(z, 3, 10, QLatin1Char('0'));
}

static QString cleanTlyText(QString s)
{
    s.replace('\r', ' ');
    s.replace('\n', ' ');
    return s.simplified();
}

static QVector<LrcLine> parseLrcFile(const QString &path, QMap<QString, QString> *meta = nullptr)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};

    const QString content = QString::fromUtf8(f.readAll());
    static const QRegularExpression metaRe(R"(^\s*\[([A-Za-z]+):([^\]]*)\]\s*$)");
    static const QRegularExpression timeRe(R"(\[(\d{1,3}:\d{2}(?:[\.,:]\d{1,3})?)\])");

    QVector<LrcLine> out;
    const QStringList rows = content.split(QRegularExpression("[\r\n]"), Qt::SkipEmptyParts);
    for (const QString &raw : rows) {
        const QString row = raw.trimmed();
        const auto mm = metaRe.match(row);
        if (mm.hasMatch()) {
            if (meta)
                meta->insert(mm.captured(1).toCaseFolded(), mm.captured(2).trimmed());
            continue;
        }

        QVector<qint64> times;
        auto it = timeRe.globalMatch(row);
        while (it.hasNext()) {
            const auto m = it.next();
            bool ok = false;
            const qint64 ms = parseLrcTimeMs(m.captured(1), &ok);
            if (ok)
                times.push_back(ms);
        }
        if (times.isEmpty())
            continue;

        QString text = row;
        text.remove(timeRe);
        text = cleanTlyText(text);
        for (qint64 ms : times)
            out.push_back({ms, text});
    }

    std::sort(out.begin(), out.end(), [](const LrcLine &a, const LrcLine &b) {
        if (a.startMs != b.startMs)
            return a.startMs < b.startMs;
        return a.text < b.text;
    });
    return out;
}

static QMap<qint64, QString> readTranslationJson(const QFileInfo &audio, const QVector<LrcLine> &lines)
{
    QMap<qint64, QString> out;
    const QString path = audio.dir().filePath(audio.completeBaseName() + ".zh-CN.json");
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return out;

    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (pe.error != QJsonParseError::NoError)
        return out;

    if (doc.isArray()) {
        int k = 0;
        const QJsonArray arr = doc.array();
        for (const auto &line : lines) {
            if (line.text.isEmpty())
                continue;
            if (k < arr.size() && !arr[k].toString().trimmed().isEmpty())
                out.insert(line.startMs, arr[k].toString().trimmed());
            ++k;
        }
    } else if (doc.isObject()) {
        const QJsonObject obj = doc.object();
        for (auto it = obj.begin(); it != obj.end(); ++it) {
            bool ok = false;
            qint64 ms = it.key().toLongLong(&ok);
            if (!ok)
                ms = parseLrcTimeMs(it.key(), &ok);
            if (ok && !it.value().toString().trimmed().isEmpty())
                out.insert(ms, it.value().toString().trimmed());
        }
    }
    return out;
}

static QString convertLrcToTly(const QFileInfo &audio, const Track &track)
{
    const QDir dir = audio.dir();
    const QString lrcPath = dir.filePath(audio.completeBaseName() + ".lrc");
    if (!QFileInfo::exists(lrcPath))
        return {};

    const QString tlyPath = dir.filePath(audio.completeBaseName() + ".tly");
    if (QFileInfo::exists(tlyPath))
        return canonicalLocalPath(tlyPath);

    QMap<QString, QString> meta;
    const QVector<LrcLine> lines = parseLrcFile(lrcPath, &meta);
    if (lines.isEmpty())
        return {};

    const QMap<qint64, QString> translations = readTranslationJson(audio, lines);

    QFile out(tlyPath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Text))
        return {};

    QTextStream ts(&out);
    ts.setEncoding(QStringConverter::Utf8);
    ts << "@title = " << cleanTlyText(track.displayTitle()) << "\n";
    if (!track.artist.trimmed().isEmpty())
        ts << "@artist = " << cleanTlyText(track.artist) << "\n";
    if (!track.album.trimmed().isEmpty())
        ts << "@album = " << cleanTlyText(track.album) << "\n";
    if (!track.genre.trimmed().isEmpty())
        ts << "@genre = " << cleanTlyText(track.genre) << "\n";
    if (meta.contains("by"))
        ts << "@lrc_by = " << cleanTlyText(meta.value("by")) << "\n";
    ts << "@source = lrc\n\n";

    for (const auto &line : lines) {
        if (line.text.isEmpty())
            continue;
        ts << "[" << formatTlyTime(line.startMs) << "]" << cleanTlyText(line.text) << "\n";
        const QString tr = translations.value(line.startMs);
        if (!tr.isEmpty())
            ts << "[" << formatTlyTime(line.startMs) << "|tr=zh-CN]" << cleanTlyText(tr) << "\n";
    }
    return canonicalLocalPath(tlyPath);
}

static QString findSidecar(const QFileInfo &audio, const QStringList &suffixes, const QStringList &baseNames)
{
    const QDir dir = audio.dir();
    const QString stem = audio.completeBaseName();

    for (const QString &ext : suffixes) {
        const QString sameStem = dir.filePath(stem + "." + ext);
        if (QFileInfo::exists(sameStem))
            return canonicalLocalPath(sameStem);

        const QString sameStemCover = dir.filePath(stem + ".cover." + ext);
        if (QFileInfo::exists(sameStemCover))
            return canonicalLocalPath(sameStemCover);
    }

    for (const QString &base : baseNames) {
        for (const QString &ext : suffixes) {
            const QString candidate = dir.filePath(base + "." + ext);
            if (QFileInfo::exists(candidate))
                return canonicalLocalPath(candidate);
        }
    }
    return {};
}

static QString findSameStemSidecar(const QFileInfo &audio, const QStringList &suffixes)
{
    const QDir dir = audio.dir();
    const QString stem = audio.completeBaseName();

    for (const QString &ext : suffixes) {
        const QString sameStem = dir.filePath(stem + "." + ext);
        if (QFileInfo::exists(sameStem))
            return canonicalLocalPath(sameStem);

        const QString sameStemCover = dir.filePath(stem + ".cover." + ext);
        if (QFileInfo::exists(sameStemCover))
            return canonicalLocalPath(sameStemCover);
    }
    return {};
}

static QString findNamedSidecar(const QFileInfo &audio, const QStringList &suffixes, const QStringList &baseNames)
{
    const QDir dir = audio.dir();
    for (const QString &base : baseNames) {
        for (const QString &ext : suffixes) {
            const QString candidate = dir.filePath(base + "." + ext);
            if (QFileInfo::exists(candidate))
                return canonicalLocalPath(candidate);
        }
    }
    return {};
}

static void inferAlbumArtistFromFolder(const QString &folderName, QString *album, QString *artist)
{
    static const QRegularExpression re(R"(^(.+?)\s+-\s+(.+)$)");
    const auto m = re.match(folderName);
    if (!m.hasMatch())
        return;
    if (album && album->trimmed().isEmpty())
        *album = m.captured(1).trimmed();
    if (artist && artist->trimmed().isEmpty())
        *artist = m.captured(2).trimmed();
}

static QJsonObject readJsonObjectFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject())
        return {};
    return doc.object();
}

static AlbumSidecar readAlbumSidecar(const QFileInfo &audio)
{
    const QDir dir = audio.dir();
    QJsonObject root;
    for (const QString &name : {QStringLiteral("album.tnux.json"), QStringLiteral("album.json")}) {
        root = readJsonObjectFile(dir.filePath(name));
        if (!root.isEmpty())
            break;
    }
    if (root.isEmpty())
        return {};

    AlbumSidecar sidecar;
    sidecar.valid = true;
    sidecar.album = textValue(root, "album");
    sidecar.artist = textValue(root, "artist");
    sidecar.genre = textValue(root, "genre");
    sidecar.year = intValue(root, "year");

    const QString cover = textValue(root, "cover");
    if (!cover.isEmpty())
        sidecar.coverPath = canonicalLocalPath(dir.filePath(cover));

    const QJsonArray tracks = root.value("tracks").toArray();
    const QString fileName = audio.fileName();
    const QString stem = audio.completeBaseName();
    for (const auto &v : tracks) {
        const QJsonObject t = v.toObject();
        const QString file = textValue(t, "file");
        const QString title = textValue(t, "title");
        if (file == fileName || file == stem || title.compare(stem, Qt::CaseInsensitive) == 0) {
            sidecar.track = t;
            break;
        }
    }
    return sidecar;
}

static QString bundledExampleMusicPath()
{
    const QStringList roots = {
        QString::fromUtf8(TNUXMUSIC_SOURCE_DIR),
        QCoreApplication::applicationDirPath(),
        QDir::currentPath(),
    };
    const QStringList rels = {
        QStringLiteral("Physics - nova9tekgrid"),
        QStringLiteral("examples/music/Physics - nova9tekgrid"),
    };

    for (const QString &root : roots) {
        if (root.trimmed().isEmpty())
            continue;
        for (const QString &rel : rels) {
            const QString path = QDir(root).filePath(rel);
            if (QFileInfo(path).isDir())
                return canonicalLocalPath(path);
        }
    }
    return {};
}

LibraryManager::LibraryManager(QObject *parent)
    : QAbstractListModel(parent)
{
    const QString root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(root);
    m_libraryPath = QDir(root).filePath("library.json");
}

int LibraryManager::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_visibleRows.size();
}

QVariant LibraryManager::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_visibleRows.size())
        return {};

    const int sourceRow = m_visibleRows[index.row()];
    if (sourceRow < 0 || sourceRow >= m_tracks.size())
        return {};

    const Track &t = m_tracks[sourceRow];
    switch (role) {
    case IdRole: return t.id;
    case TitleRole: return t.displayTitle();
    case ArtistRole: return t.artist;
    case AlbumRole: return t.album;
    case GenreRole: return t.genre;
    case YearRole: return t.year;
    case CoverPathRole: return t.coverPath;
    case CoverUrlRole: return fileUrlFromPath(t.coverPath);
    case LyricPathRole: return t.lyricPath;
    case PrimaryPathRole: return t.primaryPath();
    case PrimaryUrlRole: return fileUrlFromPath(t.primaryPath());
    case QualityCountRole: return t.qualities.size();
    case QualitiesTextRole: return t.qualitiesText();
    case SourceRowRole: return sourceRow;
    default: return {};
    }
}

QHash<int, QByteArray> LibraryManager::roleNames() const
{
    return {
        {IdRole, "trackId"},
        {TitleRole, "title"},
        {ArtistRole, "artist"},
        {AlbumRole, "album"},
        {GenreRole, "genre"},
        {YearRole, "year"},
        {CoverPathRole, "coverPath"},
        {CoverUrlRole, "coverUrl"},
        {LyricPathRole, "lyricPath"},
        {PrimaryPathRole, "primaryPath"},
        {PrimaryUrlRole, "primaryUrl"},
        {QualityCountRole, "qualityCount"},
        {QualitiesTextRole, "qualitiesText"},
        {SourceRowRole, "sourceRow"},
    };
}

void LibraryManager::setSearchQuery(const QString &query)
{
    const QString normalized = query.simplified().toCaseFolded();
    if (m_searchQuery == normalized)
        return;

    beginResetModel();
    m_searchQuery = normalized;
    rebuildVisibleRows();
    endResetModel();
    emit searchQueryChanged();
}

void LibraryManager::rebuildVisibleRows()
{
    m_visibleRows.clear();
    m_visibleRows.reserve(m_tracks.size());

    if (m_searchQuery.isEmpty()) {
        for (int i = 0; i < m_tracks.size(); ++i)
            m_visibleRows.push_back(i);
        return;
    }

    for (int i = 0; i < m_tracks.size(); ++i) {
        const Track &t = m_tracks[i];
        const QString haystack = QStringLiteral("%1 %2 %3 %4")
            .arg(t.displayTitle(), t.artist, t.album, t.genre)
            .simplified()
            .toCaseFolded();
        if (haystack.contains(m_searchQuery))
            m_visibleRows.push_back(i);
    }
}

int LibraryManager::sourceRowForDisplayRow(int displayRow) const
{
    if (displayRow < 0 || displayRow >= m_visibleRows.size())
        return -1;
    return m_visibleRows[displayRow];
}

QString LibraryManager::loadDefault()
{
    if (!QFileInfo::exists(m_libraryPath)) {
        const QString demoPath = bundledExampleMusicPath();
        if (!demoPath.isEmpty()) {
            const QString scan = scanFolder(demoPath);
            setLastMessage(QStringLiteral("已创建示例曲库：%1").arg(scan));
            return m_lastMessage;
        }
        setLastMessage(QStringLiteral("默认曲库尚未创建：%1").arg(m_libraryPath));
        return m_lastMessage;
    }

    QJsonObject obj;
    QString error;
    if (!readJsonFile(m_libraryPath, &obj, &error)) {
        setLastMessage(error);
        return error;
    }
    if (!replaceFromJsonObject(obj, &error)) {
        setLastMessage(error);
        return error;
    }
    setLastMessage(QStringLiteral("已加载默认曲库：%1 首").arg(m_tracks.size()));
    return m_lastMessage;
}

QString LibraryManager::save()
{
    QString error;
    if (!writeJsonFile(m_libraryPath, toJsonObject(), &error)) {
        setLastMessage(error);
        return error;
    }
    setLastMessage(QStringLiteral("已保存默认曲库：%1").arg(m_libraryPath));
    return m_lastMessage;
}

QString LibraryManager::scanFolder(const QString &folderUrl)
{
    const QString folder = canonicalLocalPath(folderUrl);
    QFileInfo fi(folder);
    if (!fi.exists() || !fi.isDir()) {
        const QString msg = QStringLiteral("扫描失败：不是文件夹 %1").arg(folder);
        setLastMessage(msg);
        return msg;
    }

    int addedBefore = m_tracks.size();
    int audioFiles = 0;
    int ncmFiles = 0;
    int ncmConverted = 0;
    int ncmFailed = 0;
    int ncmUnsupported = 0;
    beginResetModel();
    QDirIterator it(folder, QDir::Files | QDir::Readable, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const QFileInfo info(path);
        const QString ext = info.suffix().toLower();
        if (kEncryptedExt.contains(ext)) {
            ++ncmFiles;
            const NcmImportResult converted = NcmImportService::convertToOpenAudio(path);
            if (converted.status == NcmImportResult::Status::Converted &&
                !converted.outputAudioPath.trimmed().isEmpty() &&
                QFileInfo::exists(converted.outputAudioPath)) {
                const QFileInfo outInfo(converted.outputAudioPath);
                if (kAudioExt.contains(outInfo.suffix().toLower())) {
                    ++ncmConverted;
                    ++audioFiles;
                    mergeTrack(inferTrackFromAudioFile(converted.outputAudioPath));
                } else {
                    ++ncmFailed;
                }
            } else if (converted.status == NcmImportResult::Status::Failed) {
                ++ncmFailed;
            } else {
                ++ncmUnsupported;
            }
            continue;
        }
        if (!kAudioExt.contains(ext))
            continue;
        ++audioFiles;
        mergeTrack(inferTrackFromAudioFile(path));
    }
    sortTracks();
    rebuildVisibleRows();
    endResetModel();
    emit libraryChanged();

    save();
    const int added = m_tracks.size() - addedBefore;
    QString msg = QStringLiteral("扫描完成：发现 %1 个音频文件，新增 %2 首，曲库共 %3 首")
                      .arg(audioFiles).arg(added).arg(m_tracks.size());
    if (ncmFiles > 0) {
        msg += QStringLiteral("；NCM %1 个：转换 %2，未处理 %3，失败 %4")
                   .arg(ncmFiles)
                   .arg(ncmConverted)
                   .arg(ncmUnsupported)
                   .arg(ncmFailed);
    }
    setLastMessage(msg);
    return m_lastMessage;
}

QString LibraryManager::importLibrary(const QString &fileUrl)
{
    const QString path = canonicalLocalPath(fileUrl);
    QJsonObject obj;
    QString baseDir;
    QString error;
    if (!readLibrarySource(path, &obj, &baseDir, &error)) {
        setLastMessage(error);
        return error;
    }
    if (!replaceFromJsonObject(obj, &error, baseDir)) {
        setLastMessage(error);
        return error;
    }
    save();
    setLastMessage(QStringLiteral("已导入曲库：%1 首").arg(m_tracks.size()));
    return m_lastMessage;
}

QString LibraryManager::mergeLibrary(const QString &fileUrl)
{
    const QString path = canonicalLocalPath(fileUrl);
    QJsonObject obj;
    QString baseDir;
    QString error;
    if (!readLibrarySource(path, &obj, &baseDir, &error)) {
        setLastMessage(error);
        return error;
    }
    const int before = m_tracks.size();
    beginResetModel();
    const bool ok = mergeFromJsonObject(obj, &error, baseDir);
    sortTracks();
    rebuildVisibleRows();
    endResetModel();
    if (!ok) {
        setLastMessage(error);
        return error;
    }
    emit libraryChanged();
    save();
    setLastMessage(QStringLiteral("已合并曲库：新增 %1 首，共 %2 首").arg(m_tracks.size() - before).arg(m_tracks.size()));
    return m_lastMessage;
}

QString LibraryManager::exportLibrary(const QString &fileUrl) const
{
    const QString path = canonicalLocalPath(fileUrl);
    QString error;
    if (!writeJsonFile(path, toJsonObject(), &error))
        return error;
    return QStringLiteral("已导出曲库：%1").arg(path);
}

QString LibraryManager::exportLocalizedZip(const QString &fileUrl) const
{
    const QString path = canonicalLocalPath(fileUrl);
    const QFileInfo zipInfo(path);
    if (!zipInfo.absoluteDir().exists())
        QDir().mkpath(zipInfo.absolutePath());

    QFile zip(path);
    if (!zip.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return QStringLiteral("无法写入本地化 ZIP：%1").arg(path);

    QVector<ZipEntryInfo> entries;
    QSet<QString> usedZipNames;
    QHash<QString, QString> sourceToZipName;
    QJsonArray tracksJson;
    int copiedFiles = 0;
    int skippedFiles = 0;

    auto localizePath = [&](const QString &sourcePath, const QString &preferredName, QString *error) -> QString {
        const QString source = canonicalLocalPath(sourcePath);
        if (source.trimmed().isEmpty())
            return {};
        if (sourceToZipName.contains(source))
            return sourceToZipName.value(source);

        const QFileInfo sourceInfo(source);
        if (!sourceInfo.exists() || !sourceInfo.isFile())
            return {};

        const QString zipName = uniqueZipName(preferredName, &usedZipNames);
        if (!addZipFile(zip, source, zipName, &entries, error))
            return {};

        sourceToZipName.insert(source, zipName);
        ++copiedFiles;
        return zipName;
    };

    for (const Track &track : m_tracks) {
        Track localized = track;

        const QString artist = track.artist.trimmed().isEmpty() ? QStringLiteral("Unknown Artist") : track.artist.trimmed();
        const QString album = track.album.trimmed().isEmpty() ? QStringLiteral("Unknown Album") : track.album.trimmed();
        const QString title = track.displayTitle().trimmed().isEmpty() ? QStringLiteral("Untitled") : track.displayTitle().trimmed();
        const QString base = zipSafeName(QStringLiteral("music/%1/%2/%3").arg(artist, album, title));

        QString error;
        QVector<TrackQuality> localizedQualities;
        localizedQualities.reserve(localized.qualities.size());
        for (TrackQuality &quality : localized.qualities) {
            const QFileInfo qualityInfo(quality.path);
            const QString ext = qualityInfo.suffix().isEmpty() ? quality.codec.toLower() : qualityInfo.suffix();
            const QString preferred = base + (ext.isEmpty() ? QString() : "." + ext);
            const QString zipName = localizePath(quality.path, preferred, &error);
            if (zipName.isEmpty() && !error.isEmpty()) {
                zip.close();
                return error;
            }
            if (zipName.isEmpty()) {
                ++skippedFiles;
                continue;
            }
            quality.path = zipName;
            localizedQualities.push_back(quality);
        }
        localized.qualities = localizedQualities;

        if (!track.coverPath.trimmed().isEmpty()) {
            const QFileInfo cInfo(track.coverPath);
            const QString preferred = base + QStringLiteral(".cover") + (cInfo.suffix().isEmpty() ? QString() : "." + cInfo.suffix());
            const QString zipName = localizePath(track.coverPath, preferred, &error);
            if (zipName.isEmpty() && !error.isEmpty()) {
                zip.close();
                return error;
            }
            if (zipName.isEmpty())
                ++skippedFiles;
            else
                localized.coverPath = zipName;
        }

        if (!track.lyricPath.trimmed().isEmpty()) {
            const QFileInfo lInfo(track.lyricPath);
            const QString preferred = base + QStringLiteral(".") + (lInfo.suffix().isEmpty() ? QStringLiteral("tly") : lInfo.suffix());
            const QString zipName = localizePath(track.lyricPath, preferred, &error);
            if (zipName.isEmpty() && !error.isEmpty()) {
                zip.close();
                return error;
            }
            if (zipName.isEmpty())
                ++skippedFiles;
            else
                localized.lyricPath = zipName;
        }

        if (!localized.qualities.isEmpty())
            tracksJson.append(localized.toJson());
    }

    QJsonObject root;
    root["schema"] = QStringLiteral("tnuxmusic.localized-library.v1");
    root["app"] = QStringLiteral("tnuxmusic");
    root["version"] = 1;
    root["tracks"] = tracksJson;

    if (!addZipEntry(zip, QStringLiteral("library.json"), QJsonDocument(root).toJson(QJsonDocument::Indented), &entries)) {
        zip.close();
        return QStringLiteral("无法写入 ZIP 曲库索引：%1").arg(path);
    }

    QString error;
    if (!finishZip(zip, entries, &error)) {
        zip.close();
        return error.isEmpty() ? QStringLiteral("无法完成 ZIP：%1").arg(path) : error;
    }
    zip.close();

    return QStringLiteral("已本地化导出 ZIP：%1（%2 首，%3 个资源文件，跳过 %4 个缺失资源）")
        .arg(path)
        .arg(tracksJson.size())
        .arg(copiedFiles)
        .arg(skippedFiles);
}

QString LibraryManager::removeAlbum(const QString &artist, const QString &album)
{
    const QString a = artist.simplified().toCaseFolded();
    const QString b = album.simplified().toCaseFolded();
    if (a.isEmpty() && b.isEmpty()) {
        const QString msg = QStringLiteral("删除失败：没有选中专辑");
        setLastMessage(msg);
        return msg;
    }

    int removed = 0;
    beginResetModel();
    auto it = std::remove_if(m_tracks.begin(), m_tracks.end(), [&](const Track &t) {
        const bool hit = t.artist.simplified().toCaseFolded() == a
            && t.album.simplified().toCaseFolded() == b;
        if (hit)
            ++removed;
        return hit;
    });
    m_tracks.erase(it, m_tracks.end());
    rebuildVisibleRows();
    endResetModel();
    emit libraryChanged();

    if (removed > 0) {
        save();
        const QString msg = QStringLiteral("已从曲库删除专辑：%1 / %2（%3 首，磁盘文件未删除）")
                                .arg(artist, album)
                                .arg(removed);
        setLastMessage(msg);
        return msg;
    }

    const QString msg = QStringLiteral("没有找到专辑：%1 / %2").arg(artist, album);
    setLastMessage(msg);
    return msg;
}

const Track *LibraryManager::trackAt(int row) const
{
    if (row < 0 || row >= m_tracks.size())
        return nullptr;
    return &m_tracks[row];
}

int LibraryManager::rowOfId(const QString &id) const
{
    for (int i = 0; i < m_tracks.size(); ++i) {
        if (m_tracks[i].id == id)
            return i;
    }
    return -1;
}

QVariantMap LibraryManager::track(int row) const
{
    if (row < 0 || row >= m_tracks.size())
        return {};
    return m_tracks[row].toVariantMap();
}

int LibraryManager::rowOfTrackId(const QString &id) const
{
    return rowOfId(id);
}

QString LibraryManager::primaryPath(int row) const
{
    if (row < 0 || row >= m_tracks.size())
        return {};
    return m_tracks[row].primaryPath();
}

QString LibraryManager::lyricPath(int row) const
{
    if (row < 0 || row >= m_tracks.size())
        return {};
    return m_tracks[row].lyricPath;
}

QString LibraryManager::coverPath(int row) const
{
    if (row < 0 || row >= m_tracks.size())
        return {};
    return m_tracks[row].coverPath;
}

void LibraryManager::clear()
{
    beginResetModel();
    m_tracks.clear();
    rebuildVisibleRows();
    endResetModel();
    emit libraryChanged();
    setLastMessage(QStringLiteral("已清空当前曲库"));
}

QString LibraryManager::clearLibrary()
{
    clear();
    save();
    setLastMessage(QStringLiteral("已清空当前曲库并保存"));
    return m_lastMessage;
}

QJsonObject LibraryManager::toJsonObject() const
{
    QJsonObject root;
    root["schema"] = QStringLiteral("tnuxmusic.library.v1");
    root["app"] = QStringLiteral("tnuxmusic");
    root["version"] = 1;

    QJsonArray arr;
    for (const auto &t : m_tracks)
        arr.append(t.toJson());
    root["tracks"] = arr;
    return root;
}

bool LibraryManager::replaceFromJsonObject(const QJsonObject &obj, QString *error, const QString &baseDir)
{
    const QJsonArray arr = obj.value("tracks").toArray();
    QVector<Track> next;
    next.reserve(arr.size());
    for (const auto &v : arr) {
        Track t = Track::fromJson(v.toObject(), baseDir);
        if (!t.qualities.isEmpty())
            next.push_back(t);
    }

    beginResetModel();
    m_tracks.clear();
    for (const auto &t : next)
        mergeTrack(t);
    sortTracks();
    rebuildVisibleRows();
    endResetModel();
    emit libraryChanged();

    if (error)
        error->clear();
    return true;
}

bool LibraryManager::mergeFromJsonObject(const QJsonObject &obj, QString *error, const QString &baseDir)
{
    const QJsonArray arr = obj.value("tracks").toArray();
    for (const auto &v : arr) {
        Track t = Track::fromJson(v.toObject(), baseDir);
        if (!t.qualities.isEmpty())
            mergeTrack(t);
    }
    sortTracks();
    rebuildVisibleRows();
    if (error)
        error->clear();
    return true;
}

void LibraryManager::setLastMessage(const QString &message)
{
    if (m_lastMessage == message)
        return;
    m_lastMessage = message;
    emit lastMessageChanged();
}

bool LibraryManager::readJsonFile(const QString &path, QJsonObject *out, QString *error) const
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("无法读取 JSON：%1").arg(path);
        return false;
    }
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error)
            *error = QStringLiteral("JSON 格式错误：%1 (%2)").arg(path, pe.errorString());
        return false;
    }
    *out = doc.object();
    return true;
}

bool LibraryManager::readLibrarySource(const QString &path, QJsonObject *out, QString *baseDir, QString *error) const
{
    return readLibrarySourceInternal(path, out, baseDir, error);
}

bool LibraryManager::writeJsonFile(const QString &path, const QJsonObject &obj, QString *error) const
{
    const QFileInfo info(path);
    if (!info.absoluteDir().exists())
        QDir().mkpath(info.absolutePath());

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error)
            *error = QStringLiteral("无法写入 JSON：%1").arg(path);
        return false;
    }
    f.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    return true;
}

void LibraryManager::mergeTrack(const Track &track)
{
    if (track.qualities.isEmpty())
        return;

    const QString key = track.normalizedKey();
    for (auto &t : m_tracks) {
        if (t.normalizedKey() != key)
            continue;

        QSet<QString> paths;
        for (const auto &q : t.qualities)
            paths.insert(canonicalLocalPath(q.path));
        for (const auto &q : track.qualities) {
            if (!paths.contains(canonicalLocalPath(q.path)))
                t.qualities.push_back(q);
        }
        if (t.coverPath.isEmpty())
            t.coverPath = track.coverPath;
        if (t.lyricPath.isEmpty())
            t.lyricPath = track.lyricPath;
        if (t.genre.isEmpty())
            t.genre = track.genre;
        if (t.year == 0)
            t.year = track.year;
        if (t.trackNo == 0)
            t.trackNo = track.trackNo;
        if (t.disc <= 1)
            t.disc = track.disc;
        if (t.id.isEmpty())
            t.id = stableTrackId(t);
        return;
    }

    Track t = track;
    if (t.id.isEmpty())
        t.id = stableTrackId(t);
    m_tracks.push_back(t);
}

Track LibraryManager::inferTrackFromAudioFile(const QString &path) const
{
    const QFileInfo info(path);
    Track t;
    const AudioMetadata meta = MetadataReader::read(path);
    const AlbumSidecar sidecar = readAlbumSidecar(info);

    t.title = meta.title.isEmpty() ? cleanTitle(info.completeBaseName()) : meta.title;
    t.album = meta.album.isEmpty() ? info.dir().dirName() : meta.album;
    t.artist = meta.artist;
    t.genre = meta.genre;
    t.year = meta.year;
    t.trackNo = meta.trackNo;
    t.disc = meta.disc > 0 ? meta.disc : 1;

    if (sidecar.valid) {
        if (!textValue(sidecar.track, "title").isEmpty())
            t.title = textValue(sidecar.track, "title");
        if (!textValue(sidecar.track, "artist").isEmpty())
            t.artist = textValue(sidecar.track, "artist");
        if (!textValue(sidecar.track, "album").isEmpty())
            t.album = textValue(sidecar.track, "album");
        if (!sidecar.artist.isEmpty())
            t.artist = sidecar.artist;
        if (!sidecar.album.isEmpty())
            t.album = sidecar.album;
        if (!sidecar.genre.isEmpty())
            t.genre = sidecar.genre;
        if (sidecar.year > 0)
            t.year = sidecar.year;
        t.trackNo = intValue(sidecar.track, "track", t.trackNo);
        t.disc = intValue(sidecar.track, "disc", t.disc);
    }

    inferAlbumArtistFromFolder(info.dir().dirName(), &t.album, &t.artist);

    QDir artistDir = info.dir();
    if (t.artist.isEmpty() && artistDir.cdUp())
        t.artist = artistDir.dirName();

    TrackQuality q;
    q.path = canonicalLocalPath(path);
    q.codec = info.suffix().toUpper();
    q.label = qualityLabelFor(info);
    t.qualities.push_back(q);

    t.coverPath = sidecar.coverPath;
    if (t.coverPath.isEmpty())
        t.coverPath = findSameStemSidecar(info, kImageExt);
    if (t.coverPath.isEmpty())
        t.coverPath = writeEmbeddedCoverSidecar(info, meta);
    if (t.coverPath.isEmpty())
        t.coverPath = findNamedSidecar(info, kImageExt, kCoverNames);
    t.lyricPath = findSidecar(info, {"tly"}, {});
    if (t.lyricPath.isEmpty())
        t.lyricPath = convertLrcToTly(info, t);
    t.id = stableTrackId(t);
    return t;
}

void LibraryManager::sortTracks()
{
    QCollator collator;
    collator.setNumericMode(true);
    std::sort(m_tracks.begin(), m_tracks.end(), [&](const Track &a, const Track &b) {
        int c = collator.compare(a.artist, b.artist);
        if (c != 0)
            return c < 0;
        c = collator.compare(a.album, b.album);
        if (c != 0)
            return c < 0;
        if (a.disc != b.disc)
            return a.disc < b.disc;
        if (a.trackNo != b.trackNo)
            return a.trackNo < b.trackNo;
        return collator.compare(a.displayTitle(), b.displayTitle()) < 0;
    });
}
