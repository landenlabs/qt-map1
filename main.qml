import QtQuick
import QtQuick.Controls
import QtLocation
import QtPositioning
import MapApp

ApplicationWindow {
    id: root
    visible: true
    width: 1280
    height: 800
    title: "Qt Map Viewer  v" + appVersion

    // -----------------------------------------------------------------------
    // Map
    // -----------------------------------------------------------------------
    Map {
        id: map
        anchors.fill: parent

        plugin: Plugin {
            name: "osm"
            // Standard OSM tile server – swap this for a private tile server
            // or Stadia/MapTiler key if you hit rate limits.
            PluginParameter {
                name: "osm.mapping.custom.host"
                value: "https://tile.openstreetmap.org/"
            }
        }

        // Initial view: San Francisco
        center: QtPositioning.coordinate(37.7749, -122.4194)
        zoomLevel: 12
        minimumZoomLevel: 2
        maximumZoomLevel: 19

        // Pan via mouse/touch drag
        DragHandler {
            id: drag
            target: null
            onTranslationChanged: (delta) => {
                map.pan(-delta.x, -delta.y)
            }
        }

        // Mouse-wheel zoom (desktop)
        WheelHandler {
            id: wheelZoom
            target: null
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            onWheel: (event) => {
                const delta = event.angleDelta.y / 120.0
                map.zoomLevel = Math.max(map.minimumZoomLevel,
                                         Math.min(map.maximumZoomLevel,
                                                  map.zoomLevel + delta * 0.5))
                event.accepted = true
            }
        }

        // Pinch-to-zoom (touch / trackpad)
        PinchHandler {
            id: pinch
            target: null
            property double baseZoom: map.zoomLevel
            onActiveChanged: {
                if (active) baseZoom = map.zoomLevel
            }
            onScaleChanged: (delta) => {
                map.zoomLevel = Math.max(map.minimumZoomLevel,
                                          Math.min(map.maximumZoomLevel,
                                                   baseZoom + Math.log2(activeScale)))
            }
        }

        // -----------------------------------------------------------------------
        // Float-grid overlay rendered by the viridis GLSL fragment shader
        // -----------------------------------------------------------------------
        OverlayItem {
            id: overlay
            anchors.fill: parent
            mapItem: map
            endpoint: ""    // phase 2: set to e.g. "https://host/tiles/{z}/{x}/{y}.bin"
            dataMin: 0.0
            dataMax: 1.0
            opacity: 0.75
            visible: false  // shown by the toolbar toggle below
        }
    }

    // -----------------------------------------------------------------------
    // About dialog
    // -----------------------------------------------------------------------
    Dialog {
        id: aboutDialog
        title: "About Qt Map Viewer"
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Close

        Column {
            spacing: 10
            width: 420

            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                font.pixelSize: 15
                font.bold: true
                text: "Qt Map Viewer"
            }
            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                font.pixelSize: 13
                text: "Interactive map viewer built with Qt 6 and Qt Location.\n" +
                      "Displays OpenStreetMap tiles with pan and pinch-to-zoom.\n" +
                      "Supports a floating-point grid overlay rendered via an " +
                      "OpenGL/RHI fragment shader with a viridis colormap."
            }
            Rectangle { width: parent.width; height: 1; color: "#cccccc" }
            Grid {
                columns: 2
                columnSpacing: 12
                rowSpacing: 6
                Text { font.pixelSize: 13; font.bold: true; text: "Version:" }
                Text { font.pixelSize: 13; text: appVersion }
                Text { font.pixelSize: 13; font.bold: true; text: "Built:" }
                Text { font.pixelSize: 13; text: buildDate }
                Text { font.pixelSize: 13; font.bold: true; text: "Source:" }
                Text {
                    font.pixelSize: 13
                    color: "#1a73e8"
                    text: appUrl
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: Qt.openUrlExternally(appUrl)
                    }
                }
            }
        }
    }

    // -----------------------------------------------------------------------
    // Help button – upper left
    // -----------------------------------------------------------------------
    RoundButton {
        text: "?"
        width: 36; height: 36
        anchors {
            top: parent.top
            left: parent.left
            margins: 10
        }
        onClicked: aboutDialog.open()
    }

    // -----------------------------------------------------------------------
    // HUD: coordinates + zoom level
    // -----------------------------------------------------------------------
    Rectangle {
        id: hudBox
        anchors {
            bottom: parent.bottom
            left: parent.left
            margins: 10
        }
        color: Qt.rgba(0, 0, 0, 0.55)
        radius: 6
        width: hudText.implicitWidth + 20
        height: hudText.implicitHeight + 10

        Text {
            id: hudText
            anchors.centerIn: parent
            color: "white"
            font.pixelSize: 13
            font.family: "monospace"
            text: "lat %1  lon %2   z %3"
                .arg(map.center.latitude.toFixed(5))
                .arg(map.center.longitude.toFixed(5))
                .arg(map.zoomLevel.toFixed(2))
        }
    }

    // -----------------------------------------------------------------------
    // Toolbar
    // -----------------------------------------------------------------------
    Row {
        anchors {
            top: parent.top
            right: parent.right
            margins: 10
        }
        spacing: 6

        RoundButton {
            text: "+"
            width: 36; height: 36
            onClicked: map.zoomLevel = Math.min(map.maximumZoomLevel, map.zoomLevel + 1)
        }
        RoundButton {
            text: "−"
            width: 36; height: 36
            onClicked: map.zoomLevel = Math.max(map.minimumZoomLevel, map.zoomLevel - 1)
        }

        // Toggle the float-grid overlay on/off.
        // On first show, drawTile() generates the static test grid so the
        // shader renders immediately without needing a real endpoint.
        Button {
            id: overlayBtn
            text: overlay.visible ? "Hide overlay" : "Show overlay"
            height: 36
            onClicked: {
                if (!overlay.visible) {
                    // Trigger the test-grid render for the current centre tile
                    const z = Math.round(map.zoomLevel)
                    // Convert centre coordinate to tile x/y at zoom z
                    const lat = map.center.latitude
                    const lon = map.center.longitude
                    const n   = Math.pow(2, z)
                    const tx  = Math.floor((lon + 180.0) / 360.0 * n)
                    const ty  = Math.floor(
                        (1.0 - Math.log(
                            Math.tan(lat * Math.PI / 180.0) +
                            1.0 / Math.cos(lat * Math.PI / 180.0)) / Math.PI)
                        / 2.0 * n)
                    overlay.drawTile(z, tx, ty)
                }
                overlay.visible = !overlay.visible
            }
        }
    }
}
