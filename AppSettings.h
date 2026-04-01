#pragma once

#include <QObject>
#include <QSettings>
#include <QString>
#include <QStringList>

// AppSettings – persists user-configurable application settings via QSettings.
//
// searchPaths  – directories the app scans for external layers.json, grids.json,
//                and palettes.json files at startup and whenever the list changes.
//                External entries replace any built-in entry sharing the same name.
//
// tileUrl      – OSM plugin custom tile URL template using %z/%x/%y placeholders.
//                Read once at startup and passed directly to the map plugin.
//                Changes take effect after restarting the app.
//
// sunApiKey    – user-supplied weather API key.  When set it takes precedence
//                over the built-in compile-time key and never expires.
//                When empty the built-in key is used until it expires.
//
// daysRemaining – days until the built-in key expires (negative = expired).
//                 Computed once at construction from BUILD_UNIX_TIME and
//                 SUN_API_KEY_EXPIRY_DAYS; constant for the app lifetime.
//
// userKeyActive – true when the user has saved their own API key.

// Default base-map tile URL (Thunderforest Cycle).  Stored here so both
// AppSettings and the About dialog can reference it as the factory default.
#define kDefaultTileUrl \
    "https://api.thunderforest.com/cycle/%z/%x/%y.png?apikey=a0f8a7f923764480a31088e998b81d7f"

class AppSettings : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QStringList searchPaths
               READ  searchPaths
               WRITE setSearchPaths
               NOTIFY searchPathsChanged)

    Q_PROPERTY(QString tileUrl
               READ  tileUrl
               WRITE setTileUrl
               NOTIFY tileUrlChanged)

    Q_PROPERTY(QString sunApiKey
               READ  sunApiKey
               WRITE setSunApiKey
               NOTIFY sunApiKeyChanged)

    Q_PROPERTY(bool userKeyActive
               READ  userKeyActive
               NOTIFY sunApiKeyChanged)

    Q_PROPERTY(int daysRemaining
               READ  daysRemaining
               CONSTANT)

    Q_PROPERTY(QString mapPins
               READ  mapPins
               WRITE setMapPins
               NOTIFY mapPinsChanged)

    Q_PROPERTY(double lastCenterLat
               READ  lastCenterLat
               NOTIFY lastCenterChanged)

    Q_PROPERTY(double lastCenterLon
               READ  lastCenterLon
               NOTIFY lastCenterChanged)

public:
    explicit AppSettings(const QString &builtinApiKey,
                         int            expiryDays,
                         qint64         buildUnixTime,
                         QObject       *parent = nullptr);

    QStringList searchPaths() const;
    void        setSearchPaths(const QStringList &paths);

    QString tileUrl() const;
    void    setTileUrl(const QString &url);

    QString sunApiKey() const;
    void    setSunApiKey(const QString &key);

    bool    userKeyActive()  const;   // true when user key is set
    int     daysRemaining()  const;   // days until built-in key expires; <0 = expired

    QString mapPins()  const;
    void    setMapPins(const QString &json);

    double  lastCenterLat() const;
    double  lastCenterLon() const;
    Q_INVOKABLE void setLastCenter(double lat, double lon);

    Q_INVOKABLE QString defaultTileUrl()   const;
    Q_INVOKABLE QString effectiveApiKey()  const;  // user key, or built-in if valid, or ""

signals:
    void searchPathsChanged(const QStringList &paths);
    void tileUrlChanged(const QString &url);
    void sunApiKeyChanged(const QString &key);
    void mapPinsChanged(const QString &json);
    void lastCenterChanged();

private:
    QSettings   m_settings;
    QStringList m_searchPaths;
    QString     m_tileUrl;
    QString     m_sunApiKey;

    QString     m_mapPins;
    QString     m_builtinApiKey;
    int         m_daysRemaining = 0;   // computed once in constructor

    double      m_lastCenterLat =  37.7749;   // San Francisco default
    double      m_lastCenterLon = -122.4194;
};
