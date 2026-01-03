#pragma once

#include <QByteArray>

void logJsonUtf8Error(const QByteArray &bytes, int offset);
QByteArray sanitizeJsonUtf8ForQt(const QByteArray &bytes, bool *didSanitize = nullptr);
