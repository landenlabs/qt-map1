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
    // Shared helpers
    // -----------------------------------------------------------------------

    // Web Mercator: tile row index → latitude (degrees)
    function tileToLat(ty, z) {
        return Math.atan(Math.sinh(Math.PI * (1.0 - 2.0 * ty / Math.pow(2, z))))
               * 180.0 / Math.PI
    }

    // Compute screen-space QRectF for every tile visible at the current viewport
    // and hand them to the C++ overlay so it can position one quad per tile.
    function updateOverlayTiles() {
        var z = Math.round(map.zoomLevel)
        var n = Math.pow(2, z)

        var nw = map.toCoordinate(Qt.point(0,         0),          false)
        var se = map.toCoordinate(Qt.point(map.width, map.height), false)
        if (!nw.isValid || !se.isValid) return

        var txMin = Math.max(0,     Math.floor((nw.longitude + 180.0) / 360.0 * n))
        var txMax = Math.min(n - 1, Math.floor((se.longitude + 180.0) / 360.0 * n))
        var tyMin = Math.max(0,     Math.floor((1.0 - Math.log(Math.tan(nw.latitude * Math.PI / 180.0)
                            + 1.0 / Math.cos(nw.latitude * Math.PI / 180.0)) / Math.PI) / 2.0 * n))
        var tyMax = Math.min(n - 1, Math.floor((1.0 - Math.log(Math.tan(se.latitude * Math.PI / 180.0)
                            + 1.0 / Math.cos(se.latitude * Math.PI / 180.0)) / Math.PI) / 2.0 * n))

        var rects = []
        for (var tx = txMin; tx <= txMax; tx++) {
            for (var ty = tyMin; ty <= tyMax; ty++) {
                var lonL = tx       / n * 360.0 - 180.0
                var lonR = (tx + 1) / n * 360.0 - 180.0
                var latT = tileToLat(ty,     z)
                var latB = tileToLat(ty + 1, z)

                var ptTL = map.fromCoordinate(QtPositioning.coordinate(latT, lonL), false)
                var ptBR = map.fromCoordinate(QtPositioning.coordinate(latB, lonR), false)

                // Only render overlay on tiles where (x + y) is even (checkerboard)
                if ((tx + ty) % 2 === 0)
                    rects.push(Qt.rect(ptTL.x, ptTL.y,
                                       ptBR.x - ptTL.x, ptBR.y - ptTL.y))
            }
        }
        overlay.setVisibleTiles(rects)
    }

    // -----------------------------------------------------------------------
    // Respond to map viewport changes while overlay is active
    // -----------------------------------------------------------------------
    Connections {
        target: map
        function onCenterChanged()    { if (overlay.visible) updateOverlayTiles() }
        function onZoomLevelChanged() { if (overlay.visible) updateOverlayTiles() }
    }

    // -----------------------------------------------------------------------
    // Map
    // -----------------------------------------------------------------------
    Map {
        id: map
        anchors.fill: parent

        plugin: Plugin {
            name: "osm"
            // Use a local providers repository so the OSM plugin fetches tiles from
            // Thunderforest with the full URL template including the apikey query param.
            // providers/cycle contains the JSON with UrlTemplate using %z/%x/%y placeholders.
            PluginParameter {
                name: "osm.mapping.providersrepository.address"
                value: Qt.resolvedUrl("../providers/")
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
        // Tile boundary grid
        // -----------------------------------------------------------------------
        Canvas {
            id: tileGridCanvas
            anchors.fill: parent
            visible: false



            // Repaint whenever the viewport moves or zoom changes
            Connections {
                target: map
                function onCenterChanged()    { if (tileGridCanvas.visible) tileGridCanvas.requestPaint() }
                function onZoomLevelChanged() { if (tileGridCanvas.visible) tileGridCanvas.requestPaint() }
            }
            onVisibleChanged: if (visible) requestPaint()

            onPaint: {
                var ctx = getContext("2d")
                ctx.clearRect(0, 0, width, height)

                var z = Math.round(map.zoomLevel)
                var n = Math.pow(2, z)

                // Map corners in geographic coordinates
                var nw = map.toCoordinate(Qt.point(0, 0),            false)
                var se = map.toCoordinate(Qt.point(width, height),   false)
                if (!nw.isValid || !se.isValid) return

                // Tile index range visible on screen (add 1-tile margin)
                var txMin = Math.max(0,     Math.floor((nw.longitude + 180.0) / 360.0 * n) - 1)
                var txMax = Math.min(n - 1, Math.floor((se.longitude + 180.0) / 360.0 * n) + 1)
                var tyMin = Math.max(0,     Math.floor((1.0 - Math.log(Math.tan(nw.latitude * Math.PI / 180.0)
                                + 1.0 / Math.cos(nw.latitude * Math.PI / 180.0)) / Math.PI) / 2.0 * n) - 1)
                var tyMax = Math.min(n - 1, Math.floor((1.0 - Math.log(Math.tan(se.latitude * Math.PI / 180.0)
                                + 1.0 / Math.cos(se.latitude * Math.PI / 180.0)) / Math.PI) / 2.0 * n) + 1)

                // ── Grid lines ────────────────────────────────────────────────
                ctx.strokeStyle = "rgba(220, 30, 30, 0.85)"
                ctx.lineWidth   = 1.0
                ctx.setLineDash([])

                // Vertical lines — one per tile column boundary
                for (var tx = txMin; tx <= txMax + 1; tx++) {
                    var lon = tx / n * 360.0 - 180.0
                    var top    = map.fromCoordinate(QtPositioning.coordinate( 85.0, lon), false)
                    var bottom = map.fromCoordinate(QtPositioning.coordinate(-85.0, lon), false)
                    ctx.beginPath()
                    ctx.moveTo(top.x,    0)
                    ctx.lineTo(bottom.x, height)
                    ctx.stroke()
                }

                // Horizontal lines — one per tile row boundary
                for (var ty = tyMin; ty <= tyMax + 1; ty++) {
                    var lat  = root.tileToLat(ty, z)
                    var left  = map.fromCoordinate(QtPositioning.coordinate(lat, -180.0), false)
                    var right = map.fromCoordinate(QtPositioning.coordinate(lat,  180.0), false)
                    ctx.beginPath()
                    ctx.moveTo(0,     left.y)
                    ctx.lineTo(width, right.y)
                    ctx.stroke()
                }

                // ── Tile labels (z/x/y) ───────────────────────────────────────
                ctx.font      = "bold 11px monospace"
                ctx.fillStyle = "rgba(220, 30, 30, 0.95)"
                ctx.shadowColor   = "rgba(0,0,0,0.7)"
                ctx.shadowBlur    = 3
                ctx.shadowOffsetX = 0
                ctx.shadowOffsetY = 0

                for (var lx = txMin; lx <= txMax; lx++) {
                    for (var ly = tyMin; ly <= tyMax; ly++) {
                        var lonA = lx / n * 360.0 - 180.0
                        var lonB = (lx + 1) / n * 360.0 - 180.0
                        var latA = root.tileToLat(ly,     z)
                        var latB = root.tileToLat(ly + 1, z)
                        var cPt  = map.fromCoordinate(
                                       QtPositioning.coordinate((latA + latB) * 0.5,
                                                                (lonA + lonB) * 0.5), false)
                        if (cPt.x > 0 && cPt.x < width && cPt.y > 0 && cPt.y < height) {
                            var label = z + "/" + lx + "/" + ly
                            var tw    = ctx.measureText(label).width
                            ctx.fillText(label, cPt.x - tw * 0.5, cPt.y + 5)
                        }
                    }
                }
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
            Rectangle { width: parent.width; height: 1; color: "#cccccc" }
            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                font.pixelSize: 12
                font.bold: true
                text: "Tile Cache"
            }
            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                font.pixelSize: 12
                color: "#444444"
                text: "If old watermarked tiles appear after changing the tile provider, " +
                      "clear the disk cache:"
            }
            Text {
                width: parent.width
                wrapMode: Text.WrapAnywhere
                font.pixelSize: 12
                font.family: "monospace"
                color: "#222222"
                text: "~/Library/Caches/QtLocation/5.8/tiles/osm/"
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
    // Search bar – centred at the top
    // -----------------------------------------------------------------------
    Item {
        id: searchBar
        anchors {
            top: parent.top
            horizontalCenter: parent.horizontalCenter
            topMargin: 10
        }
        width: Math.min(480, parent.width - 200)
        height: 36

        // State: "idle" | "searching" | "found" | "notfound" | "error"
        property string searchState: "idle"
        property string errorText: ""

        function search(query) {
            if (query.trim() === "") return
            searchState = "searching"
            errorText = ""

            var xhr = new XMLHttpRequest()
            var url = "https://nominatim.openstreetmap.org/search?q="
                    + encodeURIComponent(query.trim())
                    + "&format=json&limit=1"
            xhr.open("GET", url)
            xhr.setRequestHeader("User-Agent", "qt-map1/" + appVersion)
            xhr.onreadystatechange = function() {
                if (xhr.readyState !== XMLHttpRequest.DONE) return
                if (xhr.status !== 200) {
                    searchState = "error"
                    errorText = "Network error " + xhr.status
                    return
                }
                var results = JSON.parse(xhr.responseText)
                if (!results || results.length === 0) {
                    searchState = "notfound"
                    errorText = "Not found: " + query.trim()
                    return
                }
                var lat = parseFloat(results[0].lat)
                var lon = parseFloat(results[0].lon)
                map.center = QtPositioning.coordinate(lat, lon)
                map.zoomLevel = 12
                searchState = "found"
                searchField.text = results[0].display_name
            }
            xhr.send()
        }

        // ── Background ───────────────────────────────────────────────────────
        Rectangle {
            anchors.fill: parent
            radius: 18
            color: Qt.rgba(1, 1, 1, 0.92)
            border.color: {
                if (searchBar.searchState === "notfound" || searchBar.searchState === "error")
                    return "#cc3333"
                if (searchBar.searchState === "found")
                    return "#2a9d2a"
                if (searchField.activeFocus)
                    return "#1a73e8"
                return "#cccccc"
            }
            border.width: 1.5

            layer.enabled: true
            layer.effect: null  // shadow via drop shadow below
        }

        // ── Search icon ──────────────────────────────────────────────────────
        Text {
            id: searchIcon
            anchors { left: parent.left; leftMargin: 12; verticalCenter: parent.verticalCenter }
            text: searchBar.searchState === "searching" ? "…" : "⌕"
            font.pixelSize: 17
            color: "#666666"
        }

        // ── Text input ───────────────────────────────────────────────────────
        TextField {
            id: searchField
            anchors {
                left: searchIcon.right; leftMargin: 6
                right: clearBtn.left;   rightMargin: 4
                verticalCenter: parent.verticalCenter
            }
            height: parent.height - 4
            placeholderText: "Search city, state or city, country…"
            font.pixelSize: 13
            background: Item {}   // transparent – outer Rectangle provides the bg
            verticalAlignment: TextInput.AlignVCenter
            onAccepted: searchBar.search(text)

            // Show error/notfound hint in placeholder style
            Text {
                anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                visible: searchField.text === "" &&
                         (searchBar.searchState === "notfound" ||
                          searchBar.searchState === "error")
                text: searchBar.errorText
                font.pixelSize: 12
                color: "#cc3333"
            }
        }

        // ── Clear button ─────────────────────────────────────────────────────
        Text {
            id: clearBtn
            anchors { right: parent.right; rightMargin: 10; verticalCenter: parent.verticalCenter }
            text: "✕"
            font.pixelSize: 13
            color: "#888888"
            visible: searchField.text !== ""
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    searchField.text = ""
                    searchBar.searchState = "idle"
                    searchField.forceActiveFocus()
                }
            }
        }
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
    // Zoom buttons – top-right corner
    // -----------------------------------------------------------------------
    Column {
        anchors {
            top: parent.top
            right: parent.right
            margins: 10
        }
        spacing: 4

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
    }

    // -----------------------------------------------------------------------
    // Overlay toggle buttons – vertical strip along the right edge, below zoom
    // -----------------------------------------------------------------------
    Column {
        id: overlayPanel
        anchors {
            top: parent.top
            right: parent.right
            topMargin: 10 + 36 + 4 + 36 + 12   // clear the two zoom buttons
            rightMargin: 10
        }
        spacing: 4

        // ── Grid overlay (test grid / float shader) ──────────────────────────
        Button {
            id: gridOverlayBtn
            width: 54; height: 36
            text: "Grid"
            highlighted: overlay.visible
            onClicked: {
                if (!overlay.visible) {
                    overlay.drawTile(0, 0, 0)
                    updateOverlayTiles()
                }
                overlay.visible = !overlay.visible
            }
        }

        // ── Dynamic layer buttons loaded from layers.json ────────────────────
        Repeater {
            model: overlayLayers   // QVariantList exposed by main.cpp

            delegate: Button {
                required property var modelData
                required property int index

                width: 54; height: 36
                text: modelData.name

                // Per-button toggle state (layer loading wired up in a future phase)
                property bool layerActive: false
                highlighted: layerActive

                onClicked: {
                    layerActive = !layerActive
                    // TODO: enable/disable layer render using modelData.url
                    console.log((layerActive ? "Enable" : "Disable")
                                + " layer '" + modelData.name
                                + "' url: " + modelData.url)
                }
            }
        }
    }

    // -----------------------------------------------------------------------
    // Tile-boundary grid toggle – kept as a small labelled button bottom-right
    // so it doesn't crowd the overlay strip
    // -----------------------------------------------------------------------
    Button {
        anchors {
            bottom: parent.bottom
            right: parent.right
            margins: 10
        }
        text: "Tile grid"
        height: 30
        font.pixelSize: 11
        highlighted: tileGridCanvas.visible
        onClicked: tileGridCanvas.visible = !tileGridCanvas.visible
    }
}
