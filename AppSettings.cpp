#include "AppSettings.h"
#include <QDateTime>

AppSettings::AppSettings(const QString &builtinApiKey, int expiryDays, qint64 buildUnixTime, QObject *parent)
    : QObject(parent)
    , m_settings(QStringLiteral("qt-map1"), QStringLiteral("qt-map1"))
    , m_builtinApiKey(builtinApiKey) {
    m_searchPaths = m_settings.value(QStringLiteral("searchPaths")).toStringList();

    m_tileUrl = m_settings.value(QStringLiteral("tileUrl"), QStringLiteral(kDefaultTileUrl)).toString();

    m_sunApiKey = m_settings.value(QStringLiteral("sunApiKey")).toString();

    m_mapPins = m_settings.value(QStringLiteral("mapPins"), QStringLiteral("[]")).toString();

    m_lastCenterLat = m_settings.value(QStringLiteral("lastCenterLat"), m_lastCenterLat).toDouble();
    m_lastCenterLon = m_settings.value(QStringLiteral("lastCenterLon"), m_lastCenterLon).toDouble();

    // Compute how many days remain before the built-in key expires.
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const qint64 expiryTime = buildUnixTime + static_cast<qint64>(expiryDays) * 86400LL;
    m_daysRemaining = static_cast<int>((expiryTime - now) / 86400LL);
}

// ─── searchPaths ──────────────────────────────────────────────────────────────

QStringList AppSettings::searchPaths() const {
    return m_searchPaths;
}

void AppSettings::setSearchPaths(const QStringList &paths) {
    if (m_searchPaths == paths)
        return;
    m_searchPaths = paths;
    m_settings.setValue(QStringLiteral("searchPaths"), paths);
    emit searchPathsChanged(paths);
}

// ─── tileUrl ──────────────────────────────────────────────────────────────────

QString AppSettings::tileUrl() const {
    return m_tileUrl;
}

void AppSettings::setTileUrl(const QString &url) {
    if (m_tileUrl == url)
        return;
    m_tileUrl = url;
    m_settings.setValue(QStringLiteral("tileUrl"), url);
    emit tileUrlChanged(url);
}

// ─── sunApiKey ────────────────────────────────────────────────────────────────

QString AppSettings::sunApiKey() const {
    return m_sunApiKey;
}

void AppSettings::setSunApiKey(const QString &key) {
    if (m_sunApiKey == key)
        return;
    m_sunApiKey = key;
    m_settings.setValue(QStringLiteral("sunApiKey"), key);
    emit sunApiKeyChanged(key);
}

// ─── mapPins ──────────────────────────────────────────────────────────────────

QString AppSettings::mapPins() const {
    return m_mapPins;
}

void AppSettings::setMapPins(const QString &json) {
    if (m_mapPins == json)
        return;
    m_mapPins = json;
    m_settings.setValue(QStringLiteral("mapPins"), json);
    // emit mapPinsChanged(json);
}

void AppSettings::verifyMapPins(const QString &json) {

    auto saved = m_settings.value(QStringLiteral("mapPins"), QStringLiteral("[]")).toString();
    if (json != saved) {
        setMapPins(json);
    }
}

// ─── lastCenter ───────────────────────────────────────────────────────────────

double AppSettings::lastCenterLat() const {
    return m_lastCenterLat;
}
double AppSettings::lastCenterLon() const {
    return m_lastCenterLon;
}

void AppSettings::setLastCenter(double lat, double lon) {
    if (m_lastCenterLat == lat && m_lastCenterLon == lon)
        return;
    m_lastCenterLat = lat;
    m_lastCenterLon = lon;
    m_settings.setValue(QStringLiteral("lastCenterLat"), lat);
    m_settings.setValue(QStringLiteral("lastCenterLon"), lon);
    emit lastCenterChanged();
}

// ─── Derived accessors ────────────────────────────────────────────────────────

bool AppSettings::userKeyActive() const {
    return !m_sunApiKey.isEmpty();
}

int AppSettings::daysRemaining() const {
    return m_daysRemaining;
}

QString AppSettings::effectiveApiKey() const {
    if (!m_sunApiKey.isEmpty())
        return m_sunApiKey;
    if (m_daysRemaining >= 0)
        return m_builtinApiKey;
    return QString(); // expired and no user key → deny access
}

// ─── defaultTileUrl ───────────────────────────────────────────────────────────

QString AppSettings::defaultTileUrl() const {
    return QStringLiteral(kDefaultTileUrl);
}
