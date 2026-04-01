#include "PinManager.h"
#include "AppSettings.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

PinManager::PinManager(AppSettings *settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings) {
    load();
}

QVariantList PinManager::pins() const {
    return m_pins;
}

void PinManager::addPin(const QString &name, double lat, double lon, const QString &color) {
    QVariantMap pin;
    pin[QStringLiteral("name")]     = name;
    pin[QStringLiteral("lat")]      = lat;
    pin[QStringLiteral("lon")]      = lon;
    pin[QStringLiteral("pinColor")] = color;
    m_pins.append(pin);
    emit pinsChanged();
    save();
}

void PinManager::removePin(int index) {
    if (index < 0 || index >= m_pins.size())
        return;
    m_pins.removeAt(index);
    emit pinsChanged();
    save();
}

void PinManager::setPinColor(int index, const QString &color) {
    if (index < 0 || index >= m_pins.size())
        return;
    QVariantMap pin = m_pins[index].toMap();
    pin[QStringLiteral("pinColor")] = color;
    m_pins[index] = pin;
    emit pinsChanged();
    save();
}

void PinManager::clear() {
    if (m_pins.isEmpty())
        return;
    m_pins.clear();
    emit pinsChanged();
    save();
}

void PinManager::load() {
    m_pins.clear();
    const QJsonDocument doc = QJsonDocument::fromJson(m_settings->mapPins().toUtf8());
    if (!doc.isArray())
        return;
    for (const QJsonValue &val : doc.array()) {
        if (!val.isObject())
            continue;
        const QJsonObject obj = val.toObject();
        QVariantMap pin;
        pin[QStringLiteral("name")]     = obj[QStringLiteral("name")].toString();
        pin[QStringLiteral("lat")]      = obj[QStringLiteral("lat")].toDouble();
        pin[QStringLiteral("lon")]      = obj[QStringLiteral("lon")].toDouble();
        pin[QStringLiteral("pinColor")] = obj[QStringLiteral("pinColor")].toString(QStringLiteral("#cc3333"));
        m_pins.append(pin);
    }
    // No emit – called from constructor before QML is connected
}

void PinManager::save() {
    QJsonArray arr;
    for (const QVariant &v : m_pins) {
        const QVariantMap m = v.toMap();
        QJsonObject obj;
        obj[QStringLiteral("name")]     = m[QStringLiteral("name")].toString();
        obj[QStringLiteral("lat")]      = m[QStringLiteral("lat")].toDouble();
        obj[QStringLiteral("lon")]      = m[QStringLiteral("lon")].toDouble();
        obj[QStringLiteral("pinColor")] = m[QStringLiteral("pinColor")].toString();
        arr.append(obj);
    }
    m_settings->setMapPins(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}
