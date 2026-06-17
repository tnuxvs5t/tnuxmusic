#include "NcmImportService.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <openssl/evp.h>

#include <array>
#include <memory>

namespace {

constexpr int kNcmMagicSize = 10;
constexpr int kCryptoPrefixSize = 17; // "neteasecloudmusic"
constexpr int kMetaPrefixSize = 22;   // "163 key(Don't modify):"
constexpr int kChunkSize = 64 * 1024;

const QByteArray kCoreKey = QByteArray::fromHex("687A4852416D736F356B496E62617857");
const QByteArray kMetaKey = QByteArray::fromHex("2331346C6A6B5F215C5D2630553C2728");

NcmImportResult fail(const QString &message)
{
    NcmImportResult result;
    result.status = NcmImportResult::Status::Failed;
    result.message = message;
    return result;
}

bool readLe32(QFile &file, quint32 *value)
{
    const QByteArray bytes = file.read(4);
    if (bytes.size() != 4)
        return false;

    const auto *p = reinterpret_cast<const uchar *>(bytes.constData());
    *value = quint32(p[0]) | (quint32(p[1]) << 8) | (quint32(p[2]) << 16) | (quint32(p[3]) << 24);
    return true;
}

QByteArray aes128EcbDecrypt(const QByteArray &key, const QByteArray &ciphertext)
{
    if (key.size() != 16 || ciphertext.isEmpty())
        return {};

    using EvpCtxPtr = std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)>;
    EvpCtxPtr ctx(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    if (!ctx)
        return {};

    if (EVP_DecryptInit_ex(ctx.get(),
                           EVP_aes_128_ecb(),
                           nullptr,
                           reinterpret_cast<const unsigned char *>(key.constData()),
                           nullptr)
        != 1) {
        return {};
    }

    QByteArray plaintext;
    plaintext.resize(ciphertext.size() + EVP_CIPHER_block_size(EVP_aes_128_ecb()));

    int outLen = 0;
    if (EVP_DecryptUpdate(ctx.get(),
                          reinterpret_cast<unsigned char *>(plaintext.data()),
                          &outLen,
                          reinterpret_cast<const unsigned char *>(ciphertext.constData()),
                          ciphertext.size())
        != 1) {
        return {};
    }

    int finalLen = 0;
    if (EVP_DecryptFinal_ex(ctx.get(),
                            reinterpret_cast<unsigned char *>(plaintext.data() + outLen),
                            &finalLen)
        != 1) {
        return {};
    }

    plaintext.resize(outLen + finalLen);
    return plaintext;
}

bool skipBytes(QFile &file, quint64 count)
{
    if (count == 0)
        return true;
    const qint64 target = file.pos() + qint64(count);
    return target >= 0 && file.seek(target);
}

QByteArray readXoredBlock(QFile &file, uchar xorByte, QString *error)
{
    quint32 length = 0;
    if (!readLe32(file, &length)) {
        if (error)
            *error = QStringLiteral("NCM block length is truncated");
        return {};
    }
    if (length == 0 || length > 64 * 1024 * 1024) {
        if (error)
            *error = QStringLiteral("NCM block length is invalid: %1").arg(length);
        return {};
    }

    QByteArray block = file.read(length);
    if (block.size() != int(length)) {
        if (error)
            *error = QStringLiteral("NCM block payload is truncated");
        return {};
    }
    for (char &ch : block)
        ch = char(uchar(ch) ^ xorByte);
    return block;
}

QByteArray readRc4Key(QFile &file, QString *error)
{
    const QByteArray keyBlock = readXoredBlock(file, 0x64, error);
    if (keyBlock.isEmpty())
        return {};

    QByteArray decrypted = aes128EcbDecrypt(kCoreKey, keyBlock);
    if (decrypted.size() <= kCryptoPrefixSize) {
        if (error)
            *error = QStringLiteral("NCM RC4 key decrypt failed");
        return {};
    }
    decrypted.remove(0, kCryptoPrefixSize);
    return decrypted;
}

std::array<uchar, 256> buildKeyBox(const QByteArray &key)
{
    std::array<uchar, 256> box {};
    for (int i = 0; i < 256; ++i)
        box[i] = uchar(i);

    int keyOffset = 0;
    int lastByte = 0;
    for (int i = 0; i < 256; ++i) {
        const int swap = box[i];
        const int c = (swap + lastByte + uchar(key[keyOffset])) & 0xff;
        keyOffset = (keyOffset + 1) % key.size();
        box[i] = box[c];
        box[c] = uchar(swap);
        lastByte = c;
    }
    return box;
}

QJsonObject readMetadata(QFile &file)
{
    QString ignored;
    QByteArray metaBlock = readXoredBlock(file, 0x63, &ignored);
    if (metaBlock.size() <= kMetaPrefixSize)
        return {};

    metaBlock.remove(0, kMetaPrefixSize);
    const QByteArray encrypted = QByteArray::fromBase64(metaBlock);
    const QByteArray decrypted = aes128EcbDecrypt(kMetaKey, encrypted);
    const QByteArray jsonBytes = decrypted.startsWith("music:") ? decrypted.mid(6) : decrypted;

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(jsonBytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        return {};
    return doc.object();
}

QString normalizedAudioExt(QString ext)
{
    ext = ext.trimmed().toLower();
    if (ext == "mp3" || ext == "flac" || ext == "m4a" || ext == "aac" || ext == "ogg" || ext == "opus"
        || ext == "wav") {
        return ext;
    }
    return {};
}

QString detectAudioExt(const QByteArray &head)
{
    if (head.startsWith("fLaC"))
        return QStringLiteral("flac");
    if (head.startsWith("ID3"))
        return QStringLiteral("mp3");
    if (head.size() >= 2 && uchar(head[0]) == 0xff && (uchar(head[1]) & 0xe0) == 0xe0)
        return QStringLiteral("mp3");
    if (head.startsWith("OggS"))
        return QStringLiteral("ogg");
    if (head.startsWith("RIFF") && head.mid(8, 4) == "WAVE")
        return QStringLiteral("wav");
    if (head.size() >= 12 && head.mid(4, 4) == "ftyp")
        return QStringLiteral("m4a");
    return {};
}

QString detectImageExt(const QByteArray &head)
{
    if (head.startsWith(QByteArray("\xff\xd8", 2)))
        return QStringLiteral("jpg");
    if (head.startsWith(QByteArray("\x89PNG\r\n\x1a\n", 8)))
        return QStringLiteral("png");
    if (head.startsWith("RIFF") && head.mid(8, 4) == "WEBP")
        return QStringLiteral("webp");
    if (head.startsWith("BM"))
        return QStringLiteral("bmp");
    return QStringLiteral("jpg");
}

QString uniqueOutputPath(const QFileInfo &input, const QString &ext)
{
    const QDir dir = input.dir();
    QString path = dir.filePath(input.completeBaseName() + QLatin1Char('.') + ext);
    if (QFileInfo(path).absoluteFilePath() != input.absoluteFilePath())
        return path;
    return dir.filePath(input.completeBaseName() + QStringLiteral(".decoded.") + ext);
}

void writeCoverSidecar(const QFileInfo &input, const QByteArray &cover)
{
    if (cover.isEmpty())
        return;

    const QString ext = detectImageExt(cover.left(16));
    const QString coverPath = input.dir().filePath(input.completeBaseName() + QStringLiteral(".cover.") + ext);
    const QFileInfo coverInfo(coverPath);
    if (coverInfo.exists() && coverInfo.size() > 0 && coverInfo.lastModified() >= input.lastModified())
        return;

    QSaveFile out(coverPath);
    if (!out.open(QIODevice::WriteOnly))
        return;
    if (out.write(cover) != cover.size()) {
        out.cancelWriting();
        return;
    }
    out.commit();
}

void decryptChunk(QByteArray *chunk, const std::array<uchar, 256> &box, quint64 *offset)
{
    for (int i = 0; i < chunk->size(); ++i) {
        const int j = int((*offset + 1) & 0xff);
        const int k = box[(box[j] + box[(box[j] + j) & 0xff]) & 0xff];
        (*chunk)[i] = char(uchar((*chunk)[i]) ^ k);
        ++(*offset);
    }
}

} // namespace

NcmImportResult NcmImportService::convertToOpenAudio(const QString &inputPath)
{
    const QFileInfo inputInfo(inputPath);
    if (!inputInfo.exists() || !inputInfo.isFile())
        return fail(QStringLiteral("NCM input does not exist: %1").arg(inputPath));

    QFile in(inputInfo.absoluteFilePath());
    if (!in.open(QIODevice::ReadOnly))
        return fail(QStringLiteral("Cannot open NCM file: %1").arg(inputInfo.fileName()));

    const QByteArray magic = in.read(kNcmMagicSize);
    if (magic.size() != kNcmMagicSize || !magic.startsWith("CTENFDAM")) {
        NcmImportResult result;
        result.status = NcmImportResult::Status::Unsupported;
        result.message = QStringLiteral("Not a NetEase NCM file: %1").arg(inputInfo.fileName());
        return result;
    }

    QString error;
    const QByteArray rc4Key = readRc4Key(in, &error);
    if (rc4Key.isEmpty())
        return fail(QStringLiteral("NCM key decode failed: %1").arg(error));
    const auto keyBox = buildKeyBox(rc4Key);

    const QJsonObject metadata = readMetadata(in);

    quint32 crc32 = 0;
    if (!readLe32(in, &crc32))
        return fail(QStringLiteral("NCM CRC32 field is truncated: %1").arg(inputInfo.fileName()));
    Q_UNUSED(crc32);

    if (!skipBytes(in, 5))
        return fail(QStringLiteral("NCM reserved field is truncated: %1").arg(inputInfo.fileName()));

    quint32 coverSize = 0;
    if (!readLe32(in, &coverSize))
        return fail(QStringLiteral("NCM cover length is truncated: %1").arg(inputInfo.fileName()));
    if (coverSize > 64 * 1024 * 1024)
        return fail(QStringLiteral("NCM cover payload is too large: %1").arg(inputInfo.fileName()));
    const QByteArray cover = coverSize > 0 ? in.read(coverSize) : QByteArray();
    if (cover.size() != int(coverSize))
        return fail(QStringLiteral("NCM cover payload is truncated: %1").arg(inputInfo.fileName()));
    writeCoverSidecar(inputInfo, cover);

    const qint64 audioOffset = in.pos();
    QByteArray firstChunk = in.read(kChunkSize);
    if (firstChunk.isEmpty())
        return fail(QStringLiteral("NCM audio payload is empty: %1").arg(inputInfo.fileName()));

    quint64 streamOffset = 0;
    decryptChunk(&firstChunk, keyBox, &streamOffset);

    QString ext = normalizedAudioExt(metadata.value("format").toString());
    if (ext.isEmpty())
        ext = detectAudioExt(firstChunk.left(32));
    if (ext.isEmpty())
        ext = QStringLiteral("mp3");

    const QString outputPath = uniqueOutputPath(inputInfo, ext);
    const QFileInfo outputInfo(outputPath);
    if (outputInfo.exists() && outputInfo.size() > 0 && outputInfo.lastModified() >= inputInfo.lastModified()) {
        NcmImportResult result;
        result.status = NcmImportResult::Status::Converted;
        result.outputAudioPath = outputInfo.absoluteFilePath();
        result.message = QStringLiteral("NCM already converted: %1").arg(outputInfo.fileName());
        return result;
    }

    QSaveFile out(outputPath);
    if (!out.open(QIODevice::WriteOnly))
        return fail(QStringLiteral("Cannot create output audio: %1").arg(outputPath));

    if (out.write(firstChunk) != firstChunk.size()) {
        out.cancelWriting();
        return fail(QStringLiteral("Cannot write output audio: %1").arg(outputPath));
    }

    while (!in.atEnd()) {
        QByteArray chunk = in.read(kChunkSize);
        if (chunk.isEmpty())
            break;
        decryptChunk(&chunk, keyBox, &streamOffset);
        if (out.write(chunk) != chunk.size()) {
            out.cancelWriting();
            return fail(QStringLiteral("Cannot write output audio: %1").arg(outputPath));
        }
    }

    if (!out.commit())
        return fail(QStringLiteral("Cannot finalize output audio: %1").arg(outputPath));

    NcmImportResult result;
    result.status = NcmImportResult::Status::Converted;
    result.outputAudioPath = QFileInfo(outputPath).absoluteFilePath();
    result.message = QStringLiteral("NCM converted: %1").arg(QFileInfo(outputPath).fileName());
    return result;
}
