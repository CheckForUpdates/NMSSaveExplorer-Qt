#include "core/Utf8Diagnostics.h"

#include <QDebug>
#include <QStringList>

namespace {
QString toPrintableAscii(const QByteArray &data)
{
    QByteArray out;
    out.reserve(data.size());
    for (unsigned char c : data) {
        if (c >= 32 && c <= 126) {
            out.append(static_cast<char>(c));
        } else {
            out.append('.');
        }
    }
    return QString::fromLatin1(out);
}

struct PathSegment {
    enum class Type { Key, Index };
    Type type;
    QByteArray key;
    int index = -1;
};

struct Frame {
    enum class Type { Object, Array };
    Type type;
    QByteArray pendingKey;
    int index = 0;
    bool expectingValue = false;
    bool hasPathSegment = false;
};

QString jsonPathAtOffset(const QByteArray &bytes, int offset)
{
    QVector<Frame> stack;
    QVector<PathSegment> path;
    QByteArray currentString;
    QByteArray lastString;
    bool inString = false;
    bool escape = false;
    bool justEndedString = false;

    for (int i = 0; i < bytes.size() && i <= offset; ++i) {
        unsigned char c = static_cast<unsigned char>(bytes.at(i));
        if (inString) {
            if (escape) {
                escape = false;
                currentString.append(static_cast<char>(c));
                continue;
            }
            if (c == '\\') {
                escape = true;
                continue;
            }
            if (c == '"') {
                inString = false;
                lastString = currentString;
                currentString.clear();
                justEndedString = true;
                continue;
            }
            currentString.append(static_cast<char>(c));
            continue;
        }

        if (justEndedString) {
            if (c == ':') {
                if (!stack.isEmpty() && stack.last().type == Frame::Type::Object) {
                    stack.last().pendingKey = lastString;
                }
                justEndedString = false;
            } else if (c > ' ') {
                justEndedString = false;
            }
        }

        if (c == '"') {
            inString = true;
            escape = false;
            currentString.clear();
            if (!stack.isEmpty() && stack.last().type == Frame::Type::Array) {
                stack.last().expectingValue = false;
            }
            continue;
        }

        if (c == '{') {
            Frame frame{Frame::Type::Object};
            if (!stack.isEmpty()) {
                Frame &parent = stack.last();
                if (parent.type == Frame::Type::Object && !parent.pendingKey.isEmpty()) {
                    PathSegment seg{PathSegment::Type::Key, parent.pendingKey, -1};
                    path.append(seg);
                    frame.hasPathSegment = true;
                    parent.pendingKey.clear();
                } else if (parent.type == Frame::Type::Array) {
                    PathSegment seg{PathSegment::Type::Index, QByteArray(), parent.index};
                    path.append(seg);
                    frame.hasPathSegment = true;
                    parent.expectingValue = false;
                }
            }
            stack.append(frame);
            continue;
        }

        if (c == '[') {
            Frame frame{Frame::Type::Array};
            frame.index = 0;
            frame.expectingValue = true;
            if (!stack.isEmpty()) {
                Frame &parent = stack.last();
                if (parent.type == Frame::Type::Object && !parent.pendingKey.isEmpty()) {
                    PathSegment seg{PathSegment::Type::Key, parent.pendingKey, -1};
                    path.append(seg);
                    frame.hasPathSegment = true;
                    parent.pendingKey.clear();
                } else if (parent.type == Frame::Type::Array) {
                    PathSegment seg{PathSegment::Type::Index, QByteArray(), parent.index};
                    path.append(seg);
                    frame.hasPathSegment = true;
                    parent.expectingValue = false;
                }
            }
            stack.append(frame);
            continue;
        }

        if (c == '}' || c == ']') {
            if (!stack.isEmpty()) {
                Frame frame = stack.takeLast();
                if (frame.hasPathSegment && !path.isEmpty()) {
                    path.removeLast();
                }
            }
            continue;
        }

        if (c == ',' && !stack.isEmpty()) {
            Frame &frame = stack.last();
            if (frame.type == Frame::Type::Array) {
                frame.index++;
                frame.expectingValue = true;
            } else {
                frame.pendingKey.clear();
            }
            continue;
        }

        if (!stack.isEmpty()) {
            Frame &frame = stack.last();
            if (frame.type == Frame::Type::Array && frame.expectingValue) {
                if (c > ' ') {
                    frame.expectingValue = false;
                }
            }
        }
    }

    if (inString && !stack.isEmpty()) {
        Frame &parent = stack.last();
        if (parent.type == Frame::Type::Object && !parent.pendingKey.isEmpty()) {
            PathSegment seg{PathSegment::Type::Key, parent.pendingKey, -1};
            path.append(seg);
        } else if (parent.type == Frame::Type::Array) {
            PathSegment seg{PathSegment::Type::Index, QByteArray(), parent.index};
            path.append(seg);
        }
    }

    QStringList parts;
    parts << QStringLiteral("$");
    for (const PathSegment &seg : path) {
        if (seg.type == PathSegment::Type::Key) {
            QString key = toPrintableAscii(seg.key);
            parts << QStringLiteral("[\"%1\"]").arg(key);
        } else if (seg.type == PathSegment::Type::Index) {
            parts << QStringLiteral("[%1]").arg(seg.index);
        }
    }
    return parts.join(QString());
}

int utf8SequenceLength(unsigned char lead)
{
    if (lead < 0x80) {
        return 1;
    }
    if ((lead & 0xE0) == 0xC0) {
        return 2;
    }
    if ((lead & 0xF0) == 0xE0) {
        return 3;
    }
    if ((lead & 0xF8) == 0xF0) {
        return 4;
    }
    return 0;
}

bool isValidUtf8Sequence(const QByteArray &bytes, int start, int length)
{
    if (length <= 1) {
        return length == 1;
    }
    if (start + length > bytes.size()) {
        return false;
    }
    for (int i = 1; i < length; ++i) {
        unsigned char c = static_cast<unsigned char>(bytes.at(start + i));
        if ((c & 0xC0) != 0x80) {
            return false;
        }
    }
    return true;
}
}

void logJsonUtf8Error(const QByteArray &bytes, int offset)
{
    if (bytes.isEmpty() || offset < 0) {
        return;
    }
    const int window = 48;
    int start = qMax(0, offset - window);
    int end = qMin(bytes.size(), offset + window);
    QByteArray slice = bytes.mid(start, end - start);
    QString hex = slice.toHex(' ');
    QString ascii = toPrintableAscii(slice);

    qWarning() << "JSON parse error at byte offset" << offset
               << "context bytes" << start << "-" << end;
    qWarning() << "Hex:" << hex;
    qWarning() << "ASCII:" << ascii;

    int keyStart = bytes.lastIndexOf('\"', offset);
    if (keyStart >= 0) {
        int keyEnd = bytes.indexOf('\"', keyStart + 1);
        if (keyEnd > keyStart) {
            QByteArray keyBytes = bytes.mid(keyStart + 1, keyEnd - keyStart - 1);
            qWarning() << "Nearest JSON string key fragment:" << toPrintableAscii(keyBytes);
        }
    }
    qWarning() << "Estimated JSON path:" << jsonPathAtOffset(bytes, offset);
}

QByteArray sanitizeJsonUtf8ForQt(const QByteArray &bytes, bool *didSanitize)
{
    if (didSanitize) {
        *didSanitize = false;
    }
    if (bytes.isEmpty()) {
        return bytes;
    }

    QByteArray out;
    out.reserve(bytes.size());

    bool inString = false;
    bool escape = false;
    int i = 0;
    while (i < bytes.size()) {
        unsigned char c = static_cast<unsigned char>(bytes.at(i));
        if (!inString) {
            if (c == '"') {
                inString = true;
            }
            out.append(static_cast<char>(c));
            ++i;
            continue;
        }

        if (escape) {
            out.append(static_cast<char>(c));
            escape = false;
            ++i;
            continue;
        }

        if (c == '\\') {
            out.append(static_cast<char>(c));
            escape = true;
            ++i;
            continue;
        }
        if (c == '"') {
            inString = false;
            out.append(static_cast<char>(c));
            ++i;
            continue;
        }

        int seqLen = utf8SequenceLength(c);
        if (seqLen == 0 || !isValidUtf8Sequence(bytes, i, seqLen)) {
            if (didSanitize) {
                *didSanitize = true;
            }
            out.append("\\u00");
            out.append(QByteArray::number(c, 16).rightJustified(2, '0').toUpper());
            ++i;
            continue;
        }
        out.append(bytes.mid(i, seqLen));
        i += seqLen;
    }

    return out;
}
