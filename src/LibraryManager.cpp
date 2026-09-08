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
#include <QtConcurrentRun>
#include <QUuid>

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

static void writeLe16(QFileDevice &file, quint16 value)
{
    char bytes[2] = {
        char(value & 0xff),
        char((value >> 8) & 0xff),
    };
    file.write(bytes, 2);
}

static void writeLe32(QFileDevice &file, quint32 value)
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

static bool validateLibrary(const QJsonObject &obj, QString *error);

static bool safeZipName(const QString &name)
{
    return !name.isEmpty() && name != "." && name != ".." && !name.startsWith('/')
        && !name.contains(':') && !name.contains(QChar(0))
        && !name.split('/').contains("..");
}

static bool validateZipManifest(const QJsonObject &object, QString *error, const QString &baseDir)
{
    if (!validateLibrary(object, error)) return false;
    for (const auto &value : object.value("tracks").toArray()) {
        const auto track = value.toObject();
        QStringList paths;
        paths << track.value("cover").toString() << track.value("lyrics").toString();
        for (const auto &quality : track.value("qualities").toArray())
            paths << quality.toObject().value("path").toString();
        for (QString path : paths) {
            path.replace('\\', '/');
            const QString relative = path.isEmpty() ? QString() : QDir(baseDir).relativeFilePath(canonicalLocalPath(path, baseDir));
            if (!path.isEmpty() && (!safeZipName(path) || !safeZipName(relative) || QFileInfo(baseDir).isSymLink())) {
                if (error) *error = QStringLiteral("ZIP 曲库路径必须位于包内：%1").arg(path);
                return false;
            }
        }
    }
    return true;
}

static QString normalizedZipEntryName(const QByteArray &nameBytes)
{
    QString name = QString::fromUtf8(nameBytes);
    name.replace('\\', '/');
    return QDir::cleanPath(name);
}

static bool skipZipBytes(QFileDevice &zip, quint32 size)
{
    const qint64 target = zip.pos() + qint64(size);
    return target >= 0 && target <= zip.size() && zip.seek(target);
}

static bool readStoredZipEntry(const QString &zipPath,
                               const QString &wantedName,
                               QByteArray *data,
                               QString *error)
{
    QFile zip(zipPath);
    if (!zip.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("无法读取 ZIP：%1").arg(zipPath);
        return false;
    }

    while (!zip.atEnd()) {
        const QByteArray header = zip.read(30);
        if (header.isEmpty() || header.size() < 30 || readZipLe32(header, 0) != 0x04034b50)
            break;

        const quint16 method = readZipLe16(header, 8);
        const quint32 compressedSize = readZipLe32(header, 18);
        const quint32 uncompressedSize = readZipLe32(header, 22);
        const quint16 nameLen = readZipLe16(header, 26);
        const quint16 extraLen = readZipLe16(header, 28);
        const QByteArray nameBytes = zip.read(nameLen);
        if (nameBytes.size() != nameLen || !skipZipBytes(zip, extraLen)) {
            if (error)
                *error = QStringLiteral("ZIP 文件损坏：%1").arg(zipPath);
            return false;
        }

        QString rawName = QString::fromUtf8(nameBytes);
        rawName.replace('\\', '/');
        const QString name = normalizedZipEntryName(nameBytes);
        if (!safeZipName(rawName) || (readZipLe16(header, 6) & 0x0009)) {
            if (error) *error = QStringLiteral("ZIP 包含非法路径或不支持的加密/数据描述符条目：%1").arg(name);
            return false;
        }
        if (name != wantedName) {
            if (!skipZipBytes(zip, compressedSize)) {
                if (error)
                    *error = QStringLiteral("ZIP 条目截断：%1").arg(name);
                return false;
            }
            continue;
        }

        if (method != 0) {
            if (error)
                *error = QStringLiteral("ZIP 条目使用压缩算法 %1，当前导入器只支持 store：%2")
                             .arg(method)
                             .arg(name);
            return false;
        }
        if (compressedSize != uncompressedSize) {
            if (error)
                *error = QStringLiteral("ZIP 条目大小异常：%1").arg(name);
            return false;
        }

        if (compressedSize > 64 * 1024 * 1024) {
            if (error) *error = QStringLiteral("ZIP 曲库索引超过 64 MiB");
            return false;
        }
        const QByteArray bytes = zip.read(compressedSize);
        if (bytes.size() != qint64(compressedSize)) {
            if (error)
                *error = QStringLiteral("ZIP 条目截断：%1").arg(name);
            return false;
        }
        if (~crc32Bytes(bytes) != readZipLe32(header, 14)) {
            if (error) *error = QStringLiteral("ZIP 索引 CRC 校验失败");
            return false;
        }
        if (data)
            *data = bytes;
        return true;
    }

    if (error)
        *error = QStringLiteral("ZIP 中缺少 %1：%2").arg(wantedName, zipPath);
    return false;
}

static bool extractSelectedZipEntries(const QString &zipPath,
                                      const QString &destDir,
                                      const QSet<QString> &wanted,
                                      QString *error,
                                      const std::shared_ptr<std::atomic_int> &cancelState = {})
{
    if (wanted.isEmpty())
        return true;

    QDir().mkpath(destDir);
    QFile zip(zipPath);
    if (!zip.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("无法读取 ZIP：%1").arg(zipPath);
        return false;
    }

    QSet<QString> found;
    while (!zip.atEnd()) {
        if (cancelState && cancelState->load() == 1) { if (error) *error = QStringLiteral("任务已取消"); return false; }
        const QByteArray header = zip.read(30);
        if (header.isEmpty() || header.size() < 30 || readZipLe32(header, 0) != 0x04034b50)
            break;

        const quint16 method = readZipLe16(header, 8);
        const quint32 compressedSize = readZipLe32(header, 18);
        const quint32 uncompressedSize = readZipLe32(header, 22);
        const quint16 nameLen = readZipLe16(header, 26);
        const quint16 extraLen = readZipLe16(header, 28);
        const QByteArray nameBytes = zip.read(nameLen);
        if (nameBytes.size() != nameLen || !skipZipBytes(zip, extraLen)) {
            if (error)
                *error = QStringLiteral("ZIP 文件损坏：%1").arg(zipPath);
            return false;
        }

        QString rawName = QString::fromUtf8(nameBytes);
        rawName.replace('\\', '/');
        const QString name = normalizedZipEntryName(nameBytes);
        if (!safeZipName(rawName) || (readZipLe16(header, 6) & 0x0009)) {
            if (error) *error = QStringLiteral("ZIP 包含非法路径或不支持的加密/数据描述符条目：%1").arg(name);
            return false;
        }
        if (!wanted.contains(name)) {
            if (!skipZipBytes(zip, compressedSize)) {
                if (error)
                    *error = QStringLiteral("ZIP 条目截断：%1").arg(name);
                return false;
            }
            continue;
        }

        found.insert(name);
        if (name.isEmpty() || name == "." || name == ".." || name.startsWith("../") || name.contains("/../")) {
            if (error)
                *error = QStringLiteral("ZIP 条目路径非法：%1").arg(name);
            return false;
        }
        if (method != 0) {
            if (error)
                *error = QStringLiteral("ZIP 条目使用压缩算法 %1，当前导入器只支持 store：%2")
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
        // Reject symlinks in the cache so archive writes cannot escape it.
        QString cursor = outInfo.absoluteFilePath();
        const QString root = QFileInfo(destDir).absoluteFilePath();
        if (QFileInfo(root).isSymLink()) { if (error) *error = QStringLiteral("ZIP 缓存目录不能是符号链接"); return false; }
        while (cursor != root) {
            if (!cursor.startsWith(root + '/') || QFileInfo(cursor).isSymLink()) {
                if (error) *error = QStringLiteral("ZIP 缓存路径不安全：%1").arg(name);
                return false;
            }
            cursor = QFileInfo(cursor).absolutePath();
        }
        if (!outInfo.absoluteDir().exists() && !QDir().mkpath(outInfo.absolutePath())) {
            if (error)
                *error = QStringLiteral("无法创建解包目录：%1").arg(outInfo.absolutePath());
            return false;
        }
        // Verify existing resources instead of trusting a cache hit by filename.
        QFile cached(outInfo.absoluteFilePath());
        if (cached.size() == uncompressedSize && cached.open(QIODevice::ReadOnly)) {
            quint32 cachedCrc = 0xffffffffu;
            while (!cached.atEnd()) {
                if (cancelState && cancelState->load() == 1) { if (error) *error = QStringLiteral("任务已取消"); return false; }
                const auto chunk = cached.read(128 * 1024);
                if (chunk.isEmpty() && cached.error() != QFileDevice::NoError) break;
                cachedCrc = crc32Bytes(chunk, cachedCrc);
            }
            if (cached.error() == QFileDevice::NoError && ~cachedCrc == readZipLe32(header, 14)) {
                if (!skipZipBytes(zip, compressedSize)) { if (error) *error = QStringLiteral("ZIP 条目截断"); return false; }
                continue;
            }
        }
        cached.close();
        QSaveFile out(outInfo.absoluteFilePath());
        if (!out.open(QIODevice::WriteOnly)) {
            if (error)
                *error = QStringLiteral("无法写入解包文件：%1").arg(name);
            return false;
        }

        quint32 remaining = compressedSize;
        quint32 crc = 0xffffffffu;
        while (remaining > 0) {
            if (cancelState && cancelState->load() == 1) { if (error) *error = QStringLiteral("任务已取消"); return false; }
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
            crc = crc32Bytes(chunk, crc);
            remaining -= quint32(chunk.size());
        }
        if (~crc != readZipLe32(header, 14) || !out.commit()) {
            if (error) *error = QStringLiteral("ZIP 资源校验或保存失败：%1").arg(name);
            return false;
        }
    }

    for (const QString &name : wanted) {
        if (!found.contains(name)) {
            if (error)
                *error = QStringLiteral("ZIP 中缺少曲库资源：%1").arg(name);
            return false;
        }
    }
    return true;
}

static bool writeCachedZipManifest(const QString &destDir, const QByteArray &data, QString *error)
{
    if (!QDir().mkpath(destDir)) {
        if (error)
            *error = QStringLiteral("无法创建 ZIP 缓存目录：%1").arg(destDir);
        return false;
    }

    const QString path = QDir(destDir).filePath("library.json");
    QFile existing(path);
    if (existing.open(QIODevice::ReadOnly) && existing.readAll() == data)
        return true;

    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QStringLiteral("无法写入 ZIP 曲库索引：%1").arg(destDir);
        return false;
    }
    if (out.write(data) != data.size() || !out.commit()) {
        if (error)
            *error = QStringLiteral("无法保存 ZIP 曲库索引：%1").arg(destDir);
        return false;
    }
    return true;
}

static QString zipEntryForCachedPath(const QString &destDir, const QString &path)
{
    const QString root = QDir::cleanPath(QFileInfo(destDir).absoluteFilePath());
    const QString file = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    const QString relative = QDir(root).relativeFilePath(file);
    if (relative.isEmpty() || relative == "." || relative == ".." || relative.startsWith("../"))
        return {};
    return normalizedZipEntryName(relative.toUtf8());
}

static bool readLibrarySourceInternal(const QString &path, QJsonObject *out, QString *baseDir, QString *error, const std::shared_ptr<std::atomic_int> &cancelState = {})
{
    const QFileInfo info(path);
    if (!info.exists()) {
        if (error)
            *error = QStringLiteral("文件不存在：%1").arg(path);
        return false;
    }

    if (info.suffix().toLower() == "zip") {
        const QString destDir = zipCacheDirFor(info.absoluteFilePath());
        QByteArray bytes;
        if (!readStoredZipEntry(path, "library.json", &bytes, error)) return false;
        QJsonParseError parseError;
        const auto document = QJsonDocument::fromJson(bytes, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            if (error) *error = QStringLiteral("ZIP 曲库 JSON 错误：%1").arg(parseError.errorString());
            return false;
        }
        const auto object = document.object();
        if (!validateZipManifest(object, error, destDir)) return false;
        QSet<QString> wanted;
        for (const auto &value : object.value("tracks").toArray()) {
            const auto track = value.toObject();
            QStringList resources {track.value("cover").toString(), track.value("lyrics").toString()};
            for (const auto &q : track.value("qualities").toArray()) resources << q.toObject().value("path").toString();
            for (const auto &resource : resources) {
                if (!resource.isEmpty()) wanted.insert(normalizedZipEntryName(resource.toUtf8()));
            }
        }
        if (!extractSelectedZipEntries(path, destDir, wanted, error, cancelState)
            || !writeCachedZipManifest(destDir, bytes, error)) return false;
        *out = object;
        if (baseDir) *baseDir = destDir;
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

static bool addZipEntry(QFileDevice &zip, const QString &entryName, const QByteArray &data, QVector<ZipEntryInfo> *entries)
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

static bool addZipFile(QFileDevice &zip,
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

static bool finishZip(QFileDevice &zip, const QVector<ZipEntryInfo> &entries, QString *error)
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

LibraryManager::LibraryManager(QObject *parent, const QString &libraryFile)
    : QAbstractListModel(parent)
{
    const QString root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(root);
    m_libraryPath = libraryFile.isEmpty() ? QDir(root).filePath("library.json") : QFileInfo(libraryFile).absoluteFilePath();
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
    if (m_busy) return QStringLiteral("曲库任务进行中，请稍候");
    m_operationSucceeded = false;
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
    if (!replaceFromJsonObject(obj, &error, QFileInfo(m_libraryPath).absolutePath())) {
        setLastMessage(error);
        return error;
    }
    m_operationSucceeded = true;
    setLastMessage(QStringLiteral("已加载默认曲库：%1 首").arg(m_tracks.size()));
    return m_lastMessage;
}

QString LibraryManager::save()
{
    if (m_busy) return QStringLiteral("曲库任务进行中，请稍候");
    m_operationSucceeded = false;
    QString error;
    if (!writeJsonFile(m_libraryPath, toJsonObject(), &error)) {
        setLastMessage(error);
        return error;
    }
    m_operationSucceeded = true;
    setLastMessage(QStringLiteral("已保存默认曲库：%1").arg(m_libraryPath));
    return m_lastMessage;
}

QString LibraryManager::scanFolder(const QString &folderUrl)
{
    if (!m_preparing) return performOperation("scan", folderUrl);
    if (m_busy) return QStringLiteral("曲库任务进行中，请稍候");
    m_operationSucceeded = false;
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
    QSet<QString> scanned;
    QDirIterator it(folder, QDir::Files | QDir::Readable, QDirIterator::Subdirectories);
    while (it.hasNext() && !cancelled()) {
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
                    const QString output = canonicalLocalPath(converted.outputAudioPath);
                    if (!scanned.contains(output)) {
                        scanned.insert(output);
                        ++audioFiles;
                        mergeTrack(inferTrackFromAudioFile(output));
                    }
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
        const QString audioPath = canonicalLocalPath(path);
        if (scanned.contains(audioPath)) continue;
        scanned.insert(audioPath);
        ++audioFiles;
        mergeTrack(inferTrackFromAudioFile(audioPath));
    }
    sortTracks();
    rebuildVisibleRows();
    endResetModel();
    emit libraryChanged();

    QString saveError;
    if (!persist(&saveError)) { setLastMessage(saveError); return saveError; }
    m_operationSucceeded = true;
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
    if (!m_preparing) return performOperation("import", fileUrl);
    if (m_busy) return QStringLiteral("曲库任务进行中，请稍候");
    m_operationSucceeded = false;
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
    if (!persist(&error)) { setLastMessage(error); return error; }
    m_operationSucceeded = true;
    setLastMessage(QStringLiteral("已导入曲库：%1 首").arg(m_tracks.size()));
    return m_lastMessage;
}

QString LibraryManager::mergeLibrary(const QString &fileUrl)
{
    if (!m_preparing) return performOperation("merge", fileUrl);
    if (m_busy) return QStringLiteral("曲库任务进行中，请稍候");
    m_operationSucceeded = false;
    const QString path = canonicalLocalPath(fileUrl);
    QJsonObject obj;
    QString baseDir;
    QString error;

    const bool isZip = QFileInfo(path).suffix().compare(QStringLiteral("zip"), Qt::CaseInsensitive) == 0;
    QByteArray zipManifest;
    if (isZip) {
        baseDir = zipCacheDirFor(path);
        if (!readStoredZipEntry(path, "library.json", &zipManifest, &error)) {
            setLastMessage(error); return error;
        }
        QJsonParseError parseError;
        const auto document = QJsonDocument::fromJson(zipManifest, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            error = QStringLiteral("ZIP 曲库 JSON 错误：%1").arg(parseError.errorString());
            setLastMessage(error); return error;
        }
        obj = document.object();
        if (!validateZipManifest(obj, &error, baseDir)) { setLastMessage(error); return error; }
        QSet<QString> wanted;
        for (const auto &value : obj.value("tracks").toArray()) {
            const Track incoming = Track::fromJson(value.toObject(), baseDir);
            int found = m_trackIndexByKey.value(incoming.normalizedKey(), -1);
            for (const auto &q : incoming.qualities) {
                const int byPath = m_trackIndexByPath.value(q.path, -1);
                if (byPath >= 0) { found = byPath; break; }
            }
            const Track *existing = found >= 0 ? &m_tracks[found] : nullptr;
            auto remember = [&](const QString &resource) {
                const auto entry = zipEntryForCachedPath(baseDir, resource);
                if (!entry.isEmpty()) wanted.insert(entry);
            };
            for (const auto &q : incoming.qualities) remember(q.path);
            if (!existing || existing->coverPath.isEmpty() || existing->coverPath == incoming.coverPath) remember(incoming.coverPath);
            if (!existing || existing->lyricPath.isEmpty() || existing->lyricPath == incoming.lyricPath) remember(incoming.lyricPath);
        }
        if (!extractSelectedZipEntries(path, baseDir, wanted, &error, m_cancelState)
            || !writeCachedZipManifest(baseDir, zipManifest, &error)) {
            setLastMessage(error); return error;
        }
    } else if (!readLibrarySource(path, &obj, &baseDir, &error)) {
        setLastMessage(error); return error;
    }

    const int before = m_tracks.size();
    const bool ok = mergeFromJsonObject(obj, &error, baseDir);
    if (!ok) {
        setLastMessage(error);
        return error;
    }
    if (!persist(&error)) { setLastMessage(error); return error; }
    m_operationSucceeded = true;
    setLastMessage(QStringLiteral("已合并曲库：新增 %1 首，共 %2 首").arg(m_tracks.size() - before).arg(m_tracks.size()));
    return m_lastMessage;
}

QString LibraryManager::exportLibrary(const QString &fileUrl)
{
    const QString path = canonicalLocalPath(fileUrl);
    QString error;
    if (!writeJsonFile(path, toJsonObject(), &error))
        return error;
    m_operationSucceeded = true;
    return QStringLiteral("已导出曲库：%1").arg(path);
}

QString LibraryManager::exportLocalizedZip(const QString &fileUrl)
{
    const QString path = canonicalLocalPath(fileUrl);
    const QFileInfo zipInfo(path);
    if (!zipInfo.absoluteDir().exists())
        QDir().mkpath(zipInfo.absolutePath());

    m_operationSucceeded = false;
    QSaveFile zip(path);
    if (!zip.open(QIODevice::WriteOnly))
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
        localized.coverPath.clear();
        localized.lyricPath.clear();

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
                zip.cancelWriting();
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
                zip.cancelWriting();
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
                zip.cancelWriting();
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
        zip.cancelWriting();
        return QStringLiteral("无法写入 ZIP 曲库索引：%1").arg(path);
    }

    QString error;
    if (!finishZip(zip, entries, &error)) {
        zip.cancelWriting();
        return error.isEmpty() ? QStringLiteral("无法完成 ZIP：%1").arg(path) : error;
    }
    if (!zip.commit()) return QStringLiteral("ZIP 保存失败：%1").arg(zip.errorString());
    m_operationSucceeded = true;

    return QStringLiteral("已本地化导出 ZIP：%1（%2 首，%3 个资源文件，跳过 %4 个缺失资源）")
        .arg(path)
        .arg(tracksJson.size())
        .arg(copiedFiles)
        .arg(skippedFiles);
}

QString LibraryManager::removeAlbum(const QString &artist, const QString &album)
{
    QStringList ids;
    for (const auto &t : m_tracks)
        if (t.artist.simplified().toCaseFolded() == artist.simplified().toCaseFolded()
            && t.album.simplified().toCaseFolded() == album.simplified().toCaseFolded()) ids.append(t.id);
    return removeTracks(ids);
}

const Track *LibraryManager::trackAt(int row) const
{
    if (row < 0 || row >= m_tracks.size())
        return nullptr;
    return &m_tracks[row];
}

int LibraryManager::rowOfId(const QString &id) const
{
    return m_trackIndexById.value(id, -1);
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
    if (m_busy) return;
    beginResetModel();
    m_tracks.clear();
    rebuildTrackIndex();
    rebuildVisibleRows();
    endResetModel();
    emit libraryChanged();
    setLastMessage(QStringLiteral("已清空当前曲库"));
}

QString LibraryManager::clearLibrary()
{
    if (m_busy) return QStringLiteral("曲库任务进行中，请稍候");
    m_operationSucceeded = false;
    QString error;
    if (!commitTracks({}, &error)) { setLastMessage(error); return error; }
    m_operationSucceeded = true;
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

static bool validateLibrary(const QJsonObject &obj, QString *error)
{
    const QString schema = obj.value("schema").toString();
    if (!obj.value("tracks").isArray() || (!schema.isEmpty()
        && schema != "tnuxmusic.library.v1" && schema != "tnuxmusic.localized-library.v1")) {
        if (error) *error = QStringLiteral("曲库格式无效：需要受支持的 schema 和 tracks 数组");
        return false;
    }
    QSet<QString> ids;
    for (const auto &value : obj.value("tracks").toArray()) {
        const auto track = value.toObject();
        if (!value.isObject() || !track.value("qualities").isArray() || track.value("qualities").toArray().isEmpty()) {
            if (error) *error = QStringLiteral("曲库包含无效曲目：qualities 必须是非空数组");
            return false;
        }
        for (const auto &q : track.value("qualities").toArray()) {
            if (!q.isObject() || q.toObject().value("path").toString().trimmed().isEmpty()) {
                if (error) *error = QStringLiteral("曲库包含无效音质路径");
                return false;
            }
        }
        const QString id = track.value("id").toString();
        if (!id.isEmpty() && ids.contains(id)) {
            if (error) *error = QStringLiteral("曲库包含重复曲目 ID：%1").arg(id);
            return false;
        }
        if (!id.isEmpty()) ids.insert(id);
    }
    return true;
}

bool LibraryManager::replaceFromJsonObject(const QJsonObject &obj, QString *error, const QString &baseDir)
{
    if (m_busy) { if (error) *error = QStringLiteral("曲库任务进行中"); return false; }
    if (!validateLibrary(obj, error)) return false;
    const QJsonArray arr = obj.value("tracks").toArray();

    QVector<Track> parsed;
    QSet<QString> resolvedIds;
    for (const auto &v : arr) {
        Track t = Track::fromJson(v.toObject(), baseDir);
        if (resolvedIds.contains(t.id)) {
            if (error) *error = QStringLiteral("曲库包含重复的自动生成 ID：%1").arg(t.id);
            return false;
        }
        resolvedIds.insert(t.id);
        parsed.append(std::move(t));
    }
    beginResetModel();
    m_tracks = std::move(parsed);
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
    if (m_busy) { if (error) *error = QStringLiteral("曲库任务进行中"); return false; }
    if (!validateLibrary(obj, error)) return false;
    beginResetModel();
    // Callers can merge after loading an older library or after an external
    // model reset.  Rebuilding once here keeps each incoming track lookup
    // O(1), instead of scanning and re-normalizing every existing track.
    rebuildTrackIndex();
    const QJsonArray arr = obj.value("tracks").toArray();
    for (const auto &v : arr) {
        Track t = Track::fromJson(v.toObject(), baseDir);
        if (!t.qualities.isEmpty())
            mergeTrack(t);
    }
    sortTracks();
    rebuildVisibleRows();
    endResetModel();
    emit libraryChanged();
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

void LibraryManager::rebuildTrackIndex()
{
    m_trackIndexByKey.clear();
    m_trackIndexByKey.reserve(m_tracks.size());
    m_trackIndexById.clear();
    m_trackIndexById.reserve(m_tracks.size());
    m_trackIndexByPath.clear();
    for (int i = 0; i < m_tracks.size(); ++i) {
        m_trackIndexById.insert(m_tracks[i].id, i);
        for (const auto &q : m_tracks[i].qualities) m_trackIndexByPath.insert(q.path, i);
        const QString key = m_tracks[i].normalizedKey();
        if (m_trackIndexByKey.contains(key)) m_trackIndexByKey[key] = -2;
        else m_trackIndexByKey.insert(key, i);
    }
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
    return readLibrarySourceInternal(path, out, baseDir, error, m_cancelState);
}

bool LibraryManager::writeJsonFile(const QString &path, const QJsonObject &obj, QString *error) const
{
    const QFileInfo info(path);
    if (!info.absoluteDir().exists())
        QDir().mkpath(info.absolutePath());

    if (cancelled()) { if (error) *error = QStringLiteral("任务已取消"); return false; }
    if (m_preparing && path == m_libraryPath) return true;
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QStringLiteral("无法写入 JSON：%1").arg(path);
        return false;
    }
    const QByteArray bytes = QJsonDocument(obj).toJson(QJsonDocument::Indented);
    if (m_cancelState) {
        int expected = 0;
        if (!m_cancelState->compare_exchange_strong(expected, 2) && expected == 1) {
            if (error) *error = QStringLiteral("任务已取消");
            return false;
        }
    }
    if (f.write(bytes) != bytes.size() || !f.commit()) {
        if (error) *error = QStringLiteral("保存失败：%1 (%2)").arg(path, f.errorString());
        return false;
    }
    return true;
}

void LibraryManager::mergeTrack(const Track &track)
{
    if (cancelled() || track.qualities.isEmpty())
        return;

    const QString key = track.normalizedKey();
    int found = -1;
    for (const auto &q : track.qualities) {
        found = m_trackIndexByPath.value(q.path, -1);
        if (found >= 0) break;
    }
    const int byId = m_trackIndexById.value(track.id, -1);
    if (found < 0 && byId >= 0) {
        const auto &existing = m_tracks[byId];
        const QStringList incomingOrigins = track.originKeys.isEmpty() ? QStringList{key} : track.originKeys;
        const QStringList existingOrigins = existing.originKeys.isEmpty() ? QStringList{existing.normalizedKey()} : existing.originKeys;
        for (const auto &origin : incomingOrigins)
            if (existingOrigins.contains(origin)) { found = byId; break; }
    }
    if (found < 0 && byId < 0) {
        found = m_trackIndexByKey.value(key, -1);
        if (found >= 0 && m_tracks[found].albumId != track.albumId
            && (!m_tracks[found].albumId.isEmpty() || !track.albumId.isEmpty())) found = -1;
    }
    if (found >= 0 && found < m_tracks.size()) {
        Track &t = m_tracks[found];
        const QString previousKey = t.normalizedKey();
        if (t.originKeys.isEmpty()) t.originKeys.append(t.normalizedKey());
        for (const auto &origin : track.originKeys)
            if (!t.originKeys.contains(origin)) t.originKeys.append(origin);
        QSet<QString> paths;
        for (const auto &q : t.qualities)
            paths.insert(q.path);
        for (const auto &q : track.qualities) {
            const QString &path = q.path;
            if (!paths.contains(path)) {
                t.qualities.push_back(q);
                paths.insert(path);
                m_trackIndexByPath.insert(path, found);
            }
        }
        if (t.albumArtist.isEmpty()) t.albumArtist = track.albumArtist;
        if (t.albumId.isEmpty()) t.albumId = track.albumId;
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
        const QString updatedKey = t.normalizedKey();
        if (updatedKey != previousKey) {
            // An old metadata key must not keep pointing at a different edition.
            if (m_trackIndexByKey.value(previousKey, -1) == found) m_trackIndexByKey.remove(previousKey);
            const int previous = m_trackIndexByKey.value(updatedKey, -1);
            m_trackIndexByKey.insert(updatedKey, previous == -1 || previous == found ? found : -2);
        }
        return;
    }

    Track t = track;
    if (t.id.isEmpty())
        t.id = stableTrackId(t);
    if (m_trackIndexById.contains(t.id)) t.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const int row = m_tracks.size();
    m_trackIndexById.insert(t.id, row);
    for (const auto &q : t.qualities) m_trackIndexByPath.insert(q.path, row);
    m_tracks.push_back(t);
    if (m_trackIndexByKey.contains(key)) m_trackIndexByKey[key] = -2;
    else m_trackIndexByKey.insert(key, row);
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
    t.albumArtist = meta.albumArtist;
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
        if (!sidecar.artist.isEmpty()) {
            t.albumArtist = sidecar.artist;
            if (t.artist.isEmpty()) t.artist = sidecar.artist;
        }
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
    rebuildTrackIndex();
}

LibraryManager::~LibraryManager()
{
    // A worker owns its snapshot and finishes atomic writes before shutdown.
    m_job.waitForFinished();
}

bool LibraryManager::startOperation(const QString &kind, const QString &url)
{
    if (m_busy) return false;
    const QHash<QString, QString> labels {
        {"refresh", "正在补全专辑标签"}, {"load", "正在加载曲库"}, {"scan", "正在扫描音乐与读取标签"},
        {"import", "正在导入曲库"}, {"merge", "正在合并曲库与资源"},
        {"export", "正在导出曲库"}, {"zip", "正在打包音乐资源"}
    };
    if (!labels.contains(kind)) return false;
    m_busy = true;
    m_cancelState = (kind == "scan" || kind == "import" || kind == "merge" || kind == "refresh")
        ? std::make_shared<std::atomic_int>(0) : nullptr;
    m_operation = labels.value(kind);
    setLastMessage(m_operation);
    emit busyChanged();
    const auto snapshot = m_tracks;
    const QString path = m_libraryPath;
    disconnect(&m_job, nullptr, this, nullptr);
    connect(&m_job, &QFutureWatcher<JobResult>::finished, this, [this, kind] {
        JobResult result = m_job.result();
        if (result.success && result.changed) {
            m_hasUndo = kind != "load";
            m_undoTracks = m_hasUndo ? m_tracks : QVector<Track>{};
            emit undoAvailableChanged();
            beginResetModel();
            m_tracks = std::move(result.tracks);
            rebuildTrackIndex();
            rebuildVisibleRows();
            endResetModel();
            emit libraryChanged();
        }
        m_busy = false;
        m_cancelState.reset();
        m_operation.clear();
        setLastMessage(result.message);
        emit busyChanged();
        emit operationFinished(result.success, result.message);
    });
    const auto cancelState = m_cancelState;
    m_job.setFuture(QtConcurrent::run([snapshot, path, kind, url, cancelState] {
        // This model is created, used and destroyed exclusively on this worker.
        LibraryManager worker;
        worker.m_cancelState = cancelState;
        worker.m_tracks = snapshot;
        worker.m_libraryPath = path;
        worker.rebuildTrackIndex();
        JobResult result;
        result.changed = kind != "export" && kind != "zip";
        if (kind == "load") result.message = worker.loadDefault();
        else if (kind == "refresh") result.message = worker.refreshAlbumMetadata();
        else if (kind == "scan") result.message = worker.scanFolder(url);
        else if (kind == "import") result.message = worker.importLibrary(url);
        else if (kind == "merge") result.message = worker.mergeLibrary(url);
        else if (kind == "export") result.message = worker.exportLibrary(url);
        else if (kind == "zip") result.message = worker.exportLocalizedZip(url);
        result.success = worker.m_operationSucceeded;
        if (result.success && result.changed) result.tracks = std::move(worker.m_tracks);
        return result;
    }));
    return true;
}

QString LibraryManager::performOperation(const QString &kind, const QString &url)
{
    if (m_busy) return QStringLiteral("曲库任务进行中，请稍候");
    m_operationSucceeded = false;
    LibraryManager draft;
    draft.m_preparing = true;
    draft.m_cancelState = m_cancelState;
    draft.m_libraryPath = m_libraryPath;
    draft.m_tracks = m_tracks;
    draft.rebuildTrackIndex();
    QString message;
    if (kind == "scan") message = draft.scanFolder(url);
    else if (kind == "import") message = draft.importLibrary(url);
    else if (kind == "merge") message = draft.mergeLibrary(url);
    else if (kind == "refresh") message = draft.refreshAlbumMetadata();
    QString error;
    if (draft.m_operationSucceeded) {
        if (commitTracks(std::move(draft.m_tracks), &error)) m_operationSucceeded = true;
        else message = error;
    }
    if (cancelled()) message = QStringLiteral("任务已取消，曲库保持原样；已生成的音频或缓存文件会保留");
    setLastMessage(message);
    return message;
}

void LibraryManager::cancelOperation()
{
    if (!m_cancelState || !m_busy) return;
    int expected = 0;
    if (m_cancelState->compare_exchange_strong(expected, 1)) {
        setLastMessage(QStringLiteral("正在取消，等待当前文件处理完成…"));
        emit busyChanged();
    }
}

bool LibraryManager::persist(QString *error)
{
    return writeJsonFile(m_libraryPath, toJsonObject(), error);
}

bool LibraryManager::commitTracks(QVector<Track> tracks, QString *error)
{
    QJsonObject object;
    object["schema"] = "tnuxmusic.library.v1";
    object["app"] = "tnuxmusic";
    object["version"] = 1;
    QJsonArray array;
    for (const auto &track : tracks) array.append(track.toJson());
    object["tracks"] = array;
    if (!writeJsonFile(m_libraryPath, object, error)) return false;
    m_undoTracks = m_tracks;
    m_hasUndo = true;
    emit undoAvailableChanged();
    beginResetModel();
    m_tracks = std::move(tracks);
    sortTracks();
    rebuildVisibleRows();
    endResetModel();
    emit libraryChanged();
    return true;
}

QString LibraryManager::updateAlbum(const QStringList &ids, const QString &title,
    const QString &artist, int year, const QString &albumId)
{
    if (m_busy) return QStringLiteral("曲库任务进行中，请稍候");
    if (title.trimmed().isEmpty() || ids.isEmpty() || year < 0 || year > 9999)
        return QStringLiteral("专辑名、曲目或年份无效");
    const QSet<QString> selected(ids.cbegin(), ids.cend());
    auto next = m_tracks;
    int changed = 0;
    for (auto &track : next) {
        if (!selected.contains(track.id)) continue;
        if (track.originKeys.isEmpty()) track.originKeys.append(track.normalizedKey());
        track.album = title.trimmed();
        track.albumArtist = artist.trimmed();
        track.albumId = albumId;
        track.year = year;
        ++changed;
    }
    if (changed != selected.size()) return QStringLiteral("专辑已变化，请重新选择");
    QString error;
    if (!commitTracks(std::move(next), &error)) { setLastMessage(error); return error; }
    setLastMessage(QStringLiteral("已保存专辑：%1 · %2 首").arg(title).arg(changed));
    return m_lastMessage;
}

QString LibraryManager::removeTracks(const QStringList &ids)
{
    if (m_busy) return QStringLiteral("曲库任务进行中，请稍候");
    const QSet<QString> selected(ids.cbegin(), ids.cend());
    auto next = m_tracks;
    const auto removed = next.removeIf( [&](const Track &t) { return selected.contains(t.id); });
    if (removed == 0) return QStringLiteral("没有找到需要移除的曲目");
    QString error;
    if (!commitTracks(std::move(next), &error)) { setLastMessage(error); return error; }
    setLastMessage(QStringLiteral("已从曲库移除 %1 首，磁盘文件保留").arg(removed));
    return m_lastMessage;
}

bool LibraryManager::replaceAndSave(const QJsonObject &obj, QString *error)
{
    if (m_busy) { if (error) *error = QStringLiteral("曲库任务进行中"); return false; }
    if (!validateLibrary(obj, error)) return false;
    QVector<Track> tracks;
    QSet<QString> ids;
    for (const auto &value : obj.value("tracks").toArray()) {
        auto track = Track::fromJson(value.toObject());
        if (ids.contains(track.id)) { if (error) *error = QStringLiteral("曲库包含重复的自动生成 ID"); return false; }
        ids.insert(track.id);
        tracks.append(std::move(track));
    }
    return commitTracks(std::move(tracks), error);
}

QString LibraryManager::refreshAlbumMetadata()
{
    if (!m_preparing) return performOperation("refresh", {});
    if (m_busy) return QStringLiteral("曲库任务进行中，请稍候");
    m_operationSucceeded = false;
    auto tracks = m_tracks;
    int updated = 0, missing = 0;
    for (auto &track : tracks) {
        if (cancelled()) { setLastMessage(QStringLiteral("任务已取消")); return m_lastMessage; }
        if (!track.albumId.isEmpty() || !track.albumArtist.isEmpty()) continue;
        const QString path = track.primaryPath();
        if (!QFileInfo(path).isFile()) { ++missing; continue; }
        const auto metadata = MetadataReader::read(path);
        if (!metadata.albumArtist.isEmpty()) {
            track.albumArtist = metadata.albumArtist;
            ++updated;
        }
    }
    QString error;
    if (updated > 0 && !commitTracks(std::move(tracks), &error)) { setLastMessage(error); return error; }
    m_operationSucceeded = true;
    setLastMessage(QStringLiteral("专辑标签补全完成：更新 %1 首，缺失文件 %2 首；未标记专辑艺术家的合辑可手动合并").arg(updated).arg(missing));
    return m_lastMessage;
}

QString LibraryManager::undoLastEdit()
{
    if (m_busy) return QStringLiteral("曲库任务进行中，请稍候");
    if (!m_hasUndo) return QStringLiteral("没有可撤销的整理");
    QString error;
    if (!commitTracks(m_undoTracks, &error)) { setLastMessage(error); return error; }
    m_hasUndo = false;
    m_undoTracks.clear();
    emit undoAvailableChanged();
    setLastMessage(QStringLiteral("已撤销上次整理"));
    return m_lastMessage;
}
