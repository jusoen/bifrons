#include "core/textsniff.h"

#include <QStringConverter>
#include <QStringDecoder>

namespace bifrons {

namespace {

constexpr qsizetype SniffBytes = 8192;
constexpr int MaxControlPercent = 10;
constexpr qsizetype MaxUtf8SequenceTail = 3;

bool decodesCleanly(QByteArrayView bytes)
{
    QStringDecoder strict(QStringConverter::Utf8, QStringDecoder::Flag::Stateless);
    // decode() is lazy: only converting the result to QString runs it and sets hasError().
    const QString decoded = strict.decode(bytes);
    return !strict.hasError();
}

// Strict UTF-8. If truncated, a character cut off at the very end is allowed.
bool isValidUtf8(const QByteArray &data, bool truncated)
{
    if (decodesCleanly(data)) {
        return true;
    }
    if (!truncated) {
        return false;
    }
    for (qsizetype cut = 1; cut <= MaxUtf8SequenceTail && cut < data.size(); ++cut) {
        const auto lead = static_cast<unsigned char>(data.at(data.size() - cut));
        if (lead >= 0xC0) {
            // A lead byte with too few continuation bytes after it: valid only if the rest is clean.
            return decodesCleanly(QByteArrayView(data).chopped(cut));
        }
        if ((lead & 0xC0) != 0x80) {
            return false;
        }
    }
    return false;
}

} // namespace

bool looksLikeText(const QByteArray &head)
{
    const std::optional<QStringConverter::Encoding> bom = QStringConverter::encodingForData(head);
    if (bom && *bom != QStringConverter::Utf8) {
        return true;
    }
    const qsizetype n = std::min(head.size(), SniffBytes);
    qsizetype control = 0;
    for (qsizetype i = 0; i < n; ++i) {
        const auto b = static_cast<unsigned char>(head.at(i));
        if (b == 0) {
            return false;
        }
        const bool whitespace = b == '\t' || b == '\n' || b == '\r' || b == '\f' || b == 0x1B;
        if (b < 0x20 && !whitespace) {
            ++control;
        }
    }
    return n == 0 || control * 100 <= n * MaxControlPercent;
}

QString decodeText(const QByteArray &data, QString *encodingName, bool truncated)
{
    const std::optional<QStringConverter::Encoding> bom = QStringConverter::encodingForData(data);
    if (bom) {
        QStringDecoder decoder(*bom, QStringDecoder::Flag::Stateless);
        if (encodingName != nullptr) {
            *encodingName = QString::fromLatin1(QStringConverter::nameForEncoding(*bom));
        }
        // The decoder strips the BOM by default.
        return decoder.decode(data);
    }
    if (isValidUtf8(data, truncated)) {
        if (encodingName != nullptr) {
            *encodingName = QStringLiteral("UTF-8");
        }
        QStringDecoder utf8(QStringConverter::Utf8);
        return utf8.decode(data);
    }
    QStringDecoder system(QStringConverter::System);
    if (encodingName != nullptr) {
        *encodingName = QStringLiteral("System");
    }
    return system.decode(data);
}

} // namespace bifrons
