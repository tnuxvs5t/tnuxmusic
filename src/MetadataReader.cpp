#include "MetadataReader.h"

#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

bool AudioMetadata::hasAny() const
{
    return !title.isEmpty() || !artist.isEmpty() || !album.isEmpty() || !genre.isEmpty()
        || !coverData.isEmpty() || year > 0 || trackNo > 0 || disc > 0;
}

static quint32 be32(const uchar *p)
{
    return (quint32(p[0]) << 24) | (quint32(p[1]) << 16) | (quint32(p[2]) << 8) | quint32(p[3]);
}

static quint32 be24(const uchar *p)
{
    return (quint32(p[0]) << 16) | (quint32(p[1]) << 8) | quint32(p[2]);
}

static quint32 le32(const uchar *p)
{
    return quint32(p[0]) | (quint32(p[1]) << 8) | (quint32(p[2]) << 16) | (quint32(p[3]) << 24);
}

static quint32 synchsafe32(const uchar *p)
{
    return (quint32(p[0] & 0x7f) << 21) | (quint32(p[1] & 0x7f) << 14)
        | (quint32(p[2] & 0x7f) << 7) | quint32(p[3] & 0x7f);
}

static QByteArray removeUnsynchronization(const QByteArray &data)
{
    QByteArray out;
    out.reserve(data.size());
    for (int i = 0; i < data.size(); ++i) {
        out.append(data[i]);
        if (uchar(data[i]) == 0xff && i + 1 < data.size() && data[i + 1] == '\0')
            ++i;
    }
    return out;
}

static QString decodeUtf16(const QByteArray &bytes, bool defaultBigEndian)
{
    if (bytes.isEmpty())
        return {};

    int pos = 0;
    bool be = defaultBigEndian;
    if (bytes.size() >= 2) {
        const uchar a = uchar(bytes[0]);
        const uchar b = uchar(bytes[1]);
        if (a == 0xfe && b == 0xff) {
            be = true;
            pos = 2;
        } else if (a == 0xff && b == 0xfe) {
            be = false;
            pos = 2;
        }
    }

    QString out;
    for (; pos + 1 < bytes.size(); pos += 2) {
        const uchar a = uchar(bytes[pos]);
        const uchar b = uchar(bytes[pos + 1]);
        const ushort ch = be ? ushort((a << 8) | b) : ushort((b << 8) | a);
        if (ch == 0)
            out += QLatin1Char(' ');
        else
            out += QChar(ch);
    }
    return out.simplified();
}

static QString decodeTextFrame(QByteArray data)
{
    if (data.isEmpty())
        return {};

    const uchar enc = uchar(data.front());
    data.remove(0, 1);

    QString text;
    if (enc == 0) {
        text = QString::fromLatin1(data.constData(), data.size());
    } else if (enc == 1) {
        text = decodeUtf16(data, false);
    } else if (enc == 2) {
        text = decodeUtf16(data, true);
    } else if (enc == 3) {
        text = QString::fromUtf8(data);
    } else {
        text = QString::fromUtf8(data);
    }

    text.replace(QChar::Null, QLatin1Char(' '));
    return text.simplified();
}

static QString decodeLatin1UntilNul(const QByteArray &data, int *pos)
{
    if (!pos || *pos < 0 || *pos >= data.size())
        return {};

    const int start = *pos;
    while (*pos < data.size() && data[*pos] != '\0')
        ++(*pos);

    const QString out = QString::fromLatin1(data.constData() + start, *pos - start).trimmed();
    if (*pos < data.size())
        ++(*pos);
    return out;
}

static void skipEncodedString(const QByteArray &data, int encoding, int *pos)
{
    if (!pos || *pos < 0 || *pos >= data.size())
        return;

    if (encoding == 1 || encoding == 2) {
        while (*pos + 1 < data.size()) {
            if (data[*pos] == '\0' && data[*pos + 1] == '\0') {
                *pos += 2;
                return;
            }
            *pos += 2;
        }
        *pos = data.size();
        return;
    }

    while (*pos < data.size() && data[*pos] != '\0')
        ++(*pos);
    if (*pos < data.size())
        ++(*pos);
}

static void applyPicture(AudioMetadata *meta, const QString &mimeType, const QByteArray &data, int pictureType = 0)
{
    if (!meta || data.isEmpty())
        return;

    const QString mime = mimeType.trimmed().toLower();
    if (mime.startsWith("image/") || data.startsWith(QByteArray("\xff\xd8", 2))
        || data.startsWith(QByteArray("\x89PNG\r\n\x1a\n", 8)) || data.startsWith("RIFF")
        || data.startsWith("BM")) {
        // Prefer front cover (type 3), but keep the first usable image as fallback.
        if (meta->coverData.isEmpty() || pictureType == 3) {
            meta->coverMimeType = mime;
            meta->coverData = data;
        }
    }
}

static void parseApicFrame(AudioMetadata *meta, const QByteArray &payload)
{
    if (!meta || payload.size() < 4)
        return;

    int pos = 0;
    const int encoding = uchar(payload[pos++]);
    const QString mimeType = decodeLatin1UntilNul(payload, &pos);
    if (pos >= payload.size())
        return;

    const int pictureType = uchar(payload[pos++]);
    skipEncodedString(payload, encoding, &pos);
    if (pos >= payload.size())
        return;

    applyPicture(meta, mimeType, payload.mid(pos), pictureType);
}

static int firstNumber(const QString &s)
{
    static const QRegularExpression re(R"((\d+))");
    const auto m = re.match(s);
    if (!m.hasMatch())
        return 0;
    return m.captured(1).toInt();
}

static int firstYear(const QString &s)
{
    static const QRegularExpression re(R"((\d{4}))");
    const auto m = re.match(s);
    if (!m.hasMatch())
        return 0;
    return m.captured(1).toInt();
}

static void applyField(AudioMetadata *meta, const QString &key, const QString &value)
{
    if (!meta || value.isEmpty())
        return;

    if ((key == "TIT2" || key == "TITLE") && meta->title.isEmpty())
        meta->title = value;
    else if ((key == "TPE1" || key == "ARTIST" || key == "ALBUMARTIST") && meta->artist.isEmpty())
        meta->artist = value;
    else if ((key == "TALB" || key == "ALBUM") && meta->album.isEmpty())
        meta->album = value;
    else if ((key == "TCON" || key == "GENRE") && meta->genre.isEmpty())
        meta->genre = value;
    else if ((key == "TRCK" || key == "TRACKNUMBER") && meta->trackNo == 0)
        meta->trackNo = firstNumber(value);
    else if ((key == "TPOS" || key == "DISCNUMBER") && meta->disc == 0)
        meta->disc = firstNumber(value);
    else if ((key == "TDRC" || key == "TYER" || key == "DATE" || key == "YEAR") && meta->year == 0)
        meta->year = firstYear(value);
    else if (key == "TT2" && meta->title.isEmpty())
        meta->title = value;
    else if ((key == "TP1" || key == "TP2") && meta->artist.isEmpty())
        meta->artist = value;
    else if (key == "TAL" && meta->album.isEmpty())
        meta->album = value;
    else if (key == "TCO" && meta->genre.isEmpty())
        meta->genre = value;
    else if (key == "TRK" && meta->trackNo == 0)
        meta->trackNo = firstNumber(value);
    else if (key == "TPA" && meta->disc == 0)
        meta->disc = firstNumber(value);
    else if (key == "TYE" && meta->year == 0)
        meta->year = firstYear(value);
}

static AudioMetadata readId3v2(QFile &file)
{
    AudioMetadata meta;
    if (!file.seek(0))
        return meta;

    const QByteArray header = file.read(10);
    if (header.size() != 10 || !header.startsWith("ID3"))
        return meta;

    const int major = uchar(header[3]);
    if (major < 2 || major > 4)
        return meta;

    const quint32 tagSize = synchsafe32(reinterpret_cast<const uchar *>(header.constData() + 6));
    QByteArray tag = file.read(tagSize);
    const int flags = uchar(header[5]);
    if (flags & 0x80)
        tag = removeUnsynchronization(tag);

    int pos = 0;
    if ((flags & 0x40) && major >= 3 && pos + 4 <= tag.size()) {
        const auto *p = reinterpret_cast<const uchar *>(tag.constData() + pos);
        const quint32 extSize = (major == 4) ? synchsafe32(p) : be32(p);
        const quint32 totalSize = (major == 4) ? extSize : extSize + 4;
        if (totalSize > 0 && totalSize <= quint32(tag.size()))
            pos += int(totalSize);
    }

    while (pos + (major == 2 ? 6 : 10) <= tag.size()) {
        if (major == 2) {
            if (pos + 6 > tag.size())
                break;

            const QByteArray idBytes = tag.mid(pos, 3);
            if (idBytes == QByteArray(3, '\0'))
                break;

            const QString id = QString::fromLatin1(idBytes);
            if (!id.contains(QRegularExpression("^[A-Z0-9]{3}$")))
                break;

            const quint32 frameSize = be24(reinterpret_cast<const uchar *>(tag.constData() + pos + 3));
            pos += 6;
            if (frameSize == 0 || pos + int(frameSize) > tag.size())
                break;

            const QByteArray payload = tag.mid(pos, frameSize);
            pos += frameSize;

            if (id.startsWith('T')) {
                applyField(&meta, id, decodeTextFrame(payload));
            } else if (id == "PIC" && payload.size() >= 5) {
                int ppos = 0;
                const int encoding = uchar(payload[ppos++]);
                const QString fmt = QString::fromLatin1(payload.constData() + ppos, 3).trimmed().toLower();
                ppos += 3;
                const QString mimeType = fmt == "png" ? QStringLiteral("image/png")
                    : (fmt == "bmp" ? QStringLiteral("image/bmp") : QStringLiteral("image/jpeg"));
                const int pictureType = uchar(payload[ppos++]);
                skipEncodedString(payload, encoding, &ppos);
                if (ppos < payload.size())
                    applyPicture(&meta, mimeType, payload.mid(ppos), pictureType);
            }
            continue;
        }

        const QByteArray idBytes = tag.mid(pos, 4);
        if (idBytes == QByteArray(4, '\0'))
            break;

        const QString id = QString::fromLatin1(idBytes);
        if (!id.contains(QRegularExpression("^[A-Z0-9]{4}$")))
            break;

        const uchar *p = reinterpret_cast<const uchar *>(tag.constData() + pos + 4);
        const quint32 frameSize = (major == 4) ? synchsafe32(p) : be32(p);
        pos += 10;
        if (frameSize == 0 || pos + int(frameSize) > tag.size())
            break;

        const QByteArray payload = tag.mid(pos, frameSize);
        pos += frameSize;

        if (id.startsWith('T') && id != "TXXX") {
            applyField(&meta, id, decodeTextFrame(payload));
        } else if (id == "APIC") {
            parseApicFrame(&meta, payload);
        }
    }
    return meta;
}

static AudioMetadata readFlac(QFile &file)
{
    AudioMetadata meta;
    if (!file.seek(0) || file.read(4) != "fLaC")
        return meta;

    bool last = false;
    while (!last && !file.atEnd()) {
        const QByteArray h = file.read(4);
        if (h.size() != 4)
            break;

        const uchar *hp = reinterpret_cast<const uchar *>(h.constData());
        last = (hp[0] & 0x80) != 0;
        const int type = hp[0] & 0x7f;
        const quint32 len = be24(hp + 1);
        const QByteArray block = file.read(len);
        if (block.size() != int(len))
            break;

        if (type == 6) {
            int pos = 0;
            auto readBe = [&]() -> quint32 {
                if (pos + 4 > block.size())
                    return 0;
                const quint32 v = be32(reinterpret_cast<const uchar *>(block.constData() + pos));
                pos += 4;
                return v;
            };

            const quint32 pictureType = readBe();
            const quint32 mimeLen = readBe();
            if (pos + int(mimeLen) > block.size())
                continue;
            const QString mimeType = QString::fromLatin1(block.constData() + pos, mimeLen);
            pos += mimeLen;

            const quint32 descriptionLen = readBe();
            if (pos + int(descriptionLen) > block.size())
                continue;
            pos += descriptionLen;

            readBe(); // width
            readBe(); // height
            readBe(); // color depth
            readBe(); // indexed colors

            const quint32 dataLen = readBe();
            if (pos + int(dataLen) > block.size())
                continue;
            applyPicture(&meta, mimeType, block.mid(pos, dataLen), int(pictureType));
            continue;
        }

        if (type != 4)
            continue;

        int pos = 0;
        auto readLe = [&]() -> quint32 {
            if (pos + 4 > block.size())
                return 0;
            const quint32 v = le32(reinterpret_cast<const uchar *>(block.constData() + pos));
            pos += 4;
            return v;
        };

        const quint32 vendorLen = readLe();
        pos += vendorLen;
        if (pos + 4 > block.size())
            break;

        const quint32 count = readLe();
        for (quint32 i = 0; i < count && pos + 4 <= block.size(); ++i) {
            const quint32 clen = readLe();
            if (pos + int(clen) > block.size())
                break;
            const QString comment = QString::fromUtf8(block.constData() + pos, clen);
            pos += clen;
            const int cut = comment.indexOf('=');
            if (cut <= 0)
                continue;
            applyField(&meta, comment.left(cut).trimmed().toUpper(), comment.mid(cut + 1).trimmed());
        }
    }
    return meta;
}

AudioMetadata MetadataReader::read(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};

    const QString ext = QFileInfo(path).suffix().toLower();
    if (ext == "mp3")
        return readId3v2(file);
    if (ext == "flac")
        return readFlac(file);
    return {};
}
