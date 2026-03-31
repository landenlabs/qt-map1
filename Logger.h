#pragma once

#include <QObject>
#include <QString>

// Logger – append timestamped messages to the in-app log panel and stdout.
//
// QML usage:
//   appLogger.append("Network error: " + msg)   // add a message
//   appLogger.clear()                            // clear panel
//   TextArea { text: appLogger.text }            // bind display

class Logger : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString text READ text NOTIFY textChanged)

public:
    explicit Logger(QObject *parent = nullptr);

    QString text() const;

    // Prepend [HH:MM:SS] timestamp, append to text, write to stdout.
    Q_INVOKABLE void append(const QString &message);
    Q_INVOKABLE void clear();

    // Same as append() but does NOT write to stdout.
    // Called by the Qt message handler, which writes to stdout itself.
    Q_INVOKABLE void appendSilent(const QString &message);

signals:
    void textChanged();

private:
    QString m_text;
};
