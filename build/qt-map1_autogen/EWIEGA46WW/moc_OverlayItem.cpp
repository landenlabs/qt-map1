/****************************************************************************
** Meta object code from reading C++ file 'OverlayItem.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.11.0)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../OverlayItem.h"
#include <QtNetwork/QSslError>
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'OverlayItem.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 69
#error "This file was generated using the moc from 6.11.0. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

#ifndef Q_CONSTINIT
#define Q_CONSTINIT
#endif

QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
QT_WARNING_DISABLE_GCC("-Wuseless-cast")
namespace {
struct qt_meta_tag_ZN11OverlayItemE_t {};
} // unnamed namespace

template <> constexpr inline auto OverlayItem::qt_create_metaobjectdata<qt_meta_tag_ZN11OverlayItemE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "OverlayItem",
        "mapItemChanged",
        "",
        "endpointChanged",
        "dataMinChanged",
        "dataMaxChanged",
        "onMapViewportChanged",
        "drawTile",
        "z",
        "x",
        "y",
        "setVisibleTiles",
        "QVariantList",
        "rects",
        "mapItem",
        "endpoint",
        "dataMin",
        "dataMax"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'mapItemChanged'
        QtMocHelpers::SignalData<void()>(1, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'endpointChanged'
        QtMocHelpers::SignalData<void()>(3, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'dataMinChanged'
        QtMocHelpers::SignalData<void()>(4, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'dataMaxChanged'
        QtMocHelpers::SignalData<void()>(5, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'onMapViewportChanged'
        QtMocHelpers::SlotData<void()>(6, 2, QMC::AccessPrivate, QMetaType::Void),
        // Method 'drawTile'
        QtMocHelpers::MethodData<void(int, int, int)>(7, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Int, 8 }, { QMetaType::Int, 9 }, { QMetaType::Int, 10 },
        }}),
        // Method 'setVisibleTiles'
        QtMocHelpers::MethodData<void(const QVariantList &)>(11, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 12, 13 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
        // property 'mapItem'
        QtMocHelpers::PropertyData<QObject*>(14, QMetaType::QObjectStar, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 0),
        // property 'endpoint'
        QtMocHelpers::PropertyData<QString>(15, QMetaType::QString, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 1),
        // property 'dataMin'
        QtMocHelpers::PropertyData<float>(16, QMetaType::Float, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 2),
        // property 'dataMax'
        QtMocHelpers::PropertyData<float>(17, QMetaType::Float, QMC::DefaultPropertyFlags | QMC::Writable | QMC::StdCppSet, 3),
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<OverlayItem, qt_meta_tag_ZN11OverlayItemE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject OverlayItem::staticMetaObject = { {
    QMetaObject::SuperData::link<QQuickItem::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN11OverlayItemE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN11OverlayItemE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN11OverlayItemE_t>.metaTypes,
    nullptr
} };

void OverlayItem::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<OverlayItem *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->mapItemChanged(); break;
        case 1: _t->endpointChanged(); break;
        case 2: _t->dataMinChanged(); break;
        case 3: _t->dataMaxChanged(); break;
        case 4: _t->onMapViewportChanged(); break;
        case 5: _t->drawTile((*reinterpret_cast<std::add_pointer_t<int>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<int>>(_a[3]))); break;
        case 6: _t->setVisibleTiles((*reinterpret_cast<std::add_pointer_t<QVariantList>>(_a[1]))); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (OverlayItem::*)()>(_a, &OverlayItem::mapItemChanged, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (OverlayItem::*)()>(_a, &OverlayItem::endpointChanged, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (OverlayItem::*)()>(_a, &OverlayItem::dataMinChanged, 2))
            return;
        if (QtMocHelpers::indexOfMethod<void (OverlayItem::*)()>(_a, &OverlayItem::dataMaxChanged, 3))
            return;
    }
    if (_c == QMetaObject::ReadProperty) {
        void *_v = _a[0];
        switch (_id) {
        case 0: *reinterpret_cast<QObject**>(_v) = _t->mapItem(); break;
        case 1: *reinterpret_cast<QString*>(_v) = _t->endpoint(); break;
        case 2: *reinterpret_cast<float*>(_v) = _t->dataMin(); break;
        case 3: *reinterpret_cast<float*>(_v) = _t->dataMax(); break;
        default: break;
        }
    }
    if (_c == QMetaObject::WriteProperty) {
        void *_v = _a[0];
        switch (_id) {
        case 0: _t->setMapItem(*reinterpret_cast<QObject**>(_v)); break;
        case 1: _t->setEndpoint(*reinterpret_cast<QString*>(_v)); break;
        case 2: _t->setDataMin(*reinterpret_cast<float*>(_v)); break;
        case 3: _t->setDataMax(*reinterpret_cast<float*>(_v)); break;
        default: break;
        }
    }
}

const QMetaObject *OverlayItem::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *OverlayItem::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN11OverlayItemE_t>.strings))
        return static_cast<void*>(this);
    return QQuickItem::qt_metacast(_clname);
}

int OverlayItem::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QQuickItem::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 7)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 7;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 7)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 7;
    }
    if (_c == QMetaObject::ReadProperty || _c == QMetaObject::WriteProperty
            || _c == QMetaObject::ResetProperty || _c == QMetaObject::BindableProperty
            || _c == QMetaObject::RegisterPropertyMetaType) {
        qt_static_metacall(this, _c, _id, _a);
        _id -= 4;
    }
    return _id;
}

// SIGNAL 0
void OverlayItem::mapItemChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

// SIGNAL 1
void OverlayItem::endpointChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 1, nullptr);
}

// SIGNAL 2
void OverlayItem::dataMinChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 2, nullptr);
}

// SIGNAL 3
void OverlayItem::dataMaxChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 3, nullptr);
}
QT_WARNING_POP
