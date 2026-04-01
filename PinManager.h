#pragma once

#include <QObject>
#include <QVariantList>

class AppSettings;

// PinManager – owns the list of map pins and persists them via AppSettings.
//
// Pins are stored as QVariantList of QVariantMap with keys:
//   name (QString), lat (double), lon (double), pinColor (QString)
//
// These key names are exposed as QML model roles so MapItemView delegates
// can bind to them directly via required property declarations.

class PinManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList pins READ pins NOTIFY pinsChanged)

public:
    explicit PinManager(AppSettings *settings, QObject *parent = nullptr);

    QVariantList pins() const;

    Q_INVOKABLE void addPin(const QString &name, double lat, double lon, const QString &color);
    Q_INVOKABLE void removePin(int index);
    Q_INVOKABLE void setPinColor(int index, const QString &color);
    Q_INVOKABLE void clear();

signals:
    void pinsChanged();

private:
    void load();
    void save();

    AppSettings  *m_settings;
    QVariantList  m_pins;
};
