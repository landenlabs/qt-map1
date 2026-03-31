#include "Logger.h"

#include <QTime>
#include <cstdio>

Logger::Logger(QObject *parent) : QObject(parent) {}

QString Logger::text() const { return m_text; }

// Build one HTML line: yellow fixed-width timestamp + HTML-escaped message body.
static QString buildHtmlLine(const QString &ts, const QString &message)
{
    return QStringLiteral("<span style=\"color:#ffff00;\">[") + ts
         + QStringLiteral("]</span> ")
         + message.toHtmlEscaped();
}

void Logger::append(const QString &message)
{
    const QString ts = QTime::currentTime().toString("HH:mm:ss");

    fprintf(stdout, "[%s] %s\n", qPrintable(ts), qPrintable(message));
    fflush(stdout);

    if (!m_text.isEmpty())
        m_text += QStringLiteral("<br>");
    m_text += buildHtmlLine(ts, message);

    emit textChanged();
}

void Logger::appendSilent(const QString &message)
{
    const QString ts = QTime::currentTime().toString("HH:mm:ss");

    if (!m_text.isEmpty())
        m_text += QStringLiteral("<br>");
    m_text += buildHtmlLine(ts, message);

    emit textChanged();
}

void Logger::clear()
{
    m_text.clear();
    emit textChanged();
}
