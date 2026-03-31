#pragma once

#include <QObject>
#include <QSettings>
#include <QStringList>

// AppSettings – persists user-configurable application settings via QSettings.
//
// searchPaths – directories the app scans for external layers.json, grids.json,
//               and palettes.json files at startup and whenever the list changes.
//               External entries replace any built-in entry sharing the same name.

class AppSettings : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QStringList searchPaths
               READ  searchPaths
               WRITE setSearchPaths
               NOTIFY searchPathsChanged)

public:
    explicit AppSettings(QObject *parent = nullptr);

    QStringList searchPaths() const;
    void        setSearchPaths(const QStringList &paths);

signals:
    void searchPathsChanged(const QStringList &paths);

private:
    QSettings   m_settings;
    QStringList m_searchPaths;
};
