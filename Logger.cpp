#include "Logger.h"

#include <QTime>
#include <cstdio>

Logger::Logger(QObject *parent) : QObject(parent) {}

QString Logger::text() const { return m_text; }

void Logger::append(const QString &message)
{
    const QString ts   = QTime::currentTime().toString("[HH:mm:ss] ");
    const QString line = ts + message;

    if (!m_text.isEmpty())
        m_text += '\n';
    m_text += line;

    fprintf(stdout, "%s\n", qPrintable(line));
    fflush(stdout);

    emit textChanged();
}

void Logger::clear()
{
    m_text.clear();
    emit textChanged();
}
