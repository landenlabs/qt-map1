#include "AppSettings.h"

AppSettings::AppSettings(QObject *parent)
    : QObject(parent)
    , m_settings(QStringLiteral("qt-map1"), QStringLiteral("qt-map1"))
{
    m_searchPaths = m_settings.value(QStringLiteral("searchPaths"))
                               .toStringList();
}

QStringList AppSettings::searchPaths() const
{
    return m_searchPaths;
}

void AppSettings::setSearchPaths(const QStringList &paths)
{
    if (m_searchPaths == paths) return;
    m_searchPaths = paths;
    m_settings.setValue(QStringLiteral("searchPaths"), paths);
    emit searchPathsChanged(paths);
}
