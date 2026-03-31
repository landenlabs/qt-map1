import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
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

        var tiles = []
        for (var tx = txMin; tx <= txMax; tx++) {
            for (var ty = tyMin; ty <= tyMax; ty++) {
                var lonL = tx       / n * 360.0 - 180.0
                var lonR = (tx + 1) / n * 360.0 - 180.0
                var latT = tileToLat(ty,     z)
                var latB = tileToLat(ty + 1, z)

                var ptTL = map.fromCoordinate(QtPositioning.coordinate(latT, lonL), false)
                var ptBR = map.fromCoordinate(QtPositioning.coordinate(latB, lonR), false)

                tiles.push({
                    z: z, x: tx, y: ty,
                    rect: Qt.rect(ptTL.x, ptTL.y,
                                  ptBR.x - ptTL.x, ptBR.y - ptTL.y)
                })
            }
        }
        overlay.setVisibleTiles(tiles)
    }

    // Refresh tile positions for all visible weather layer overlays
    function refreshWeatherOverlays() {
        for (var i = 0; i < weatherOverlays.count; i++) {
            var item = weatherOverlays.itemAt(i)
            if (item && item.visible) item.refresh()
        }
    }

    // -----------------------------------------------------------------------
    // Respond to map viewport changes while overlay is active
    // -----------------------------------------------------------------------
    Connections {
        target: map
        function onCenterChanged()    {
            if (overlay.visible) updateOverlayTiles()
            refreshWeatherOverlays()
        }
        function onZoomLevelChanged() {
            if (overlay.visible) updateOverlayTiles()
            refreshWeatherOverlays()
        }
    }

    // -----------------------------------------------------------------------
    // Main layout: map pane (top) + log pane (bottom), draggable divider
    // -----------------------------------------------------------------------
    SplitView {
        id: splitView
        anchors.fill: parent
        orientation: Qt.Vertical

        handle: Rectangle {
            implicitHeight: 5
            color: SplitHandle.hovered || SplitHandle.pressed ? "#555555" : "#2c2c2c"
        }

        // ── Map pane ──────────────────────────────────────────────────────
        Item {
            id: mapPane
            SplitView.fillHeight: true
            SplitView.minimumHeight: 200

            // ---------------------------------------------------------------
            // Map
            // ---------------------------------------------------------------
            Map {
                id: map
                anchors.fill: parent

                plugin: Plugin {
                    name: "osm"
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

                // -------------------------------------------------------
                // Tile boundary grid
                // -------------------------------------------------------
                Canvas {
                    id: tileGridCanvas
                    anchors.fill: parent
                    visible: false

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

                        var nw = map.toCoordinate(Qt.point(0, 0),          false)
                        var se = map.toCoordinate(Qt.point(width, height), false)
                        if (!nw.isValid || !se.isValid) return

                        var txMin = Math.max(0,     Math.floor((nw.longitude + 180.0) / 360.0 * n) - 1)
                        var txMax = Math.min(n - 1, Math.floor((se.longitude + 180.0) / 360.0 * n) + 1)
                        var tyMin = Math.max(0,     Math.floor((1.0 - Math.log(Math.tan(nw.latitude * Math.PI / 180.0)
                                        + 1.0 / Math.cos(nw.latitude * Math.PI / 180.0)) / Math.PI) / 2.0 * n) - 1)
                        var tyMax = Math.min(n - 1, Math.floor((1.0 - Math.log(Math.tan(se.latitude * Math.PI / 180.0)
                                        + 1.0 / Math.cos(se.latitude * Math.PI / 180.0)) / Math.PI) / 2.0 * n) + 1)

                        ctx.strokeStyle = "rgba(220, 30, 30, 0.85)"
                        ctx.lineWidth   = 1.0
                        ctx.setLineDash([])

                        for (var tx = txMin; tx <= txMax + 1; tx++) {
                            var lon = tx / n * 360.0 - 180.0
                            var top    = map.fromCoordinate(QtPositioning.coordinate( 85.0, lon), false)
                            var bottom = map.fromCoordinate(QtPositioning.coordinate(-85.0, lon), false)
                            ctx.beginPath()
                            ctx.moveTo(top.x,    0)
                            ctx.lineTo(bottom.x, height)
                            ctx.stroke()
                        }

                        for (var ty = tyMin; ty <= tyMax + 1; ty++) {
                            var lat   = root.tileToLat(ty, z)
                            var left  = map.fromCoordinate(QtPositioning.coordinate(lat, -180.0), false)
                            var right = map.fromCoordinate(QtPositioning.coordinate(lat,  180.0), false)
                            ctx.beginPath()
                            ctx.moveTo(0,     left.y)
                            ctx.lineTo(width, right.y)
                            ctx.stroke()
                        }

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

                // -------------------------------------------------------
                // Float-grid overlay rendered by the viridis GLSL shader
                // -------------------------------------------------------
                OverlayItem {
                    id: overlay
                    anchors.fill: parent
                    mapItem: map
                    endpoint: ""
                    dataMin: 0.0
                    dataMax: 1.0
                    opacity: 0.75
                    visible: false
                }

                // -------------------------------------------------------
                // Weather PNG tile overlays – one Item per layer entry.
                // Indexed identically to the layer buttons Repeater so
                // weatherOverlays.itemAt(index) matches layerRepeater.itemAt(index).
                // -------------------------------------------------------
                Repeater {
                    id: weatherOverlays
                    model: layerManager.layers

                    Item {
                        id: weatherOverlayItem
                        anchors.fill: parent
                        visible: false
                        opacity: 0.7

                        required property int index
                        property string urlTemplate: ""
                        property var tiles: []

                        function refresh() {
                            if (!visible || urlTemplate === "") { tiles = []; return }
                            var z  = Math.round(map.zoomLevel)
                            var n  = Math.pow(2, z)
                            var nw = map.toCoordinate(Qt.point(0, 0),                          false)
                            var se = map.toCoordinate(Qt.point(map.width, map.height), false)
                            if (!nw.isValid || !se.isValid) { tiles = []; return }

                            var txMin = Math.max(0,     Math.floor((nw.longitude + 180.0) / 360.0 * n))
                            var txMax = Math.min(n - 1, Math.floor((se.longitude + 180.0) / 360.0 * n))
                            var tyMin = Math.max(0,     Math.floor((1.0 - Math.log(
                                            Math.tan(nw.latitude * Math.PI / 180.0)
                                            + 1.0 / Math.cos(nw.latitude * Math.PI / 180.0))
                                            / Math.PI) / 2.0 * n))
                            var tyMax = Math.min(n - 1, Math.floor((1.0 - Math.log(
                                            Math.tan(se.latitude * Math.PI / 180.0)
                                            + 1.0 / Math.cos(se.latitude * Math.PI / 180.0))
                                            / Math.PI) / 2.0 * n))

                            var newTiles = []
                            for (var tx = txMin; tx <= txMax; tx++) {
                                for (var ty = tyMin; ty <= tyMax; ty++) {
                                    var lonL = tx       / n * 360.0 - 180.0
                                    var lonR = (tx + 1) / n * 360.0 - 180.0
                                    var latT = root.tileToLat(ty,     z)
                                    var latB = root.tileToLat(ty + 1, z)
                                    var ptTL = map.fromCoordinate(
                                                   QtPositioning.coordinate(latT, lonL), false)
                                    var ptBR = map.fromCoordinate(
                                                   QtPositioning.coordinate(latB, lonR), false)
                                    var tileUrl = urlTemplate
                                                    .replace("{z}", z)
                                                    .replace("{x}", tx)
                                                    .replace("{y}", ty)
                                    newTiles.push({
                                        x: ptTL.x, y: ptTL.y,
                                        w: ptBR.x - ptTL.x,
                                        h: ptBR.y - ptTL.y,
                                        url: tileUrl
                                    })
                                }
                            }
                            tiles = newTiles
                        }

                        Repeater {
                            model: weatherOverlayItem.tiles
                            Image {
                                x:      modelData.x
                                y:      modelData.y
                                width:  modelData.w
                                height: modelData.h
                                source: modelData.url
                                fillMode: Image.Stretch
                                smooth: true
                                cache: true
                                asynchronous: true
                                onStatusChanged: {
                                    if (status === Image.Error)
                                        appLogger.append("Tile load error: url=" + source)
                                }
                            }
                        }
                    }
                }
            }

            // ---------------------------------------------------------------
            // About dialog
            // ---------------------------------------------------------------
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

            // ---------------------------------------------------------------
            // Help button – upper left
            // ---------------------------------------------------------------
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

            // ---------------------------------------------------------------
            // Search bar – centred at the top
            // ---------------------------------------------------------------
            Item {
                id: searchBar
                anchors {
                    top: parent.top
                    horizontalCenter: parent.horizontalCenter
                    topMargin: 10
                }
                width: Math.min(480, parent.width - 200)
                height: 36

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
                            appLogger.append("Search error: url=" + url + " code=" + xhr.status)
                            return
                        }
                        var results = JSON.parse(xhr.responseText)
                        if (!results || results.length === 0) {
                            searchState = "notfound"
                            errorText = "Not found: " + query.trim()
                            appLogger.append("Search not found: " + query.trim())
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
                    layer.effect: null
                }

                Text {
                    id: searchIcon
                    anchors { left: parent.left; leftMargin: 12; verticalCenter: parent.verticalCenter }
                    text: searchBar.searchState === "searching" ? "…" : "⌕"
                    font.pixelSize: 17
                    color: "#666666"
                }

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
                    background: Item {}
                    verticalAlignment: TextInput.AlignVCenter
                    onAccepted: searchBar.search(text)

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

            // ---------------------------------------------------------------
            // HUD: coordinates + zoom level – bottom left
            // ---------------------------------------------------------------
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

            // ---------------------------------------------------------------
            // Zoom buttons – top-right corner
            // ---------------------------------------------------------------
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

            // ---------------------------------------------------------------
            // Bottom-centre button panel – horizontal row, centred.
            // Wraps only when the map is narrower than the total button width.
            //
            // Width is computed from model counts and fixed button sizes rather
            // than from Flow.implicitWidth, which would create a binding loop
            // (Flow uses width to compute layout → sets implicitWidth → width
            // re-evaluates → Qt zeros the width → buttons stack vertically).
            // ---------------------------------------------------------------
            Button {
                id: testBtn
                anchors {
                    bottom: tileGridBtn.top
                    right: parent.right
                    rightMargin: 10
                    bottomMargin: 4
                }
                text: "Test"
                height: 30
                font.pixelSize: 11
                onClicked: {
                    // Use URLs from the first grids.json entry
                    if (gridManager.grids.length > 0) {
                        var g = gridManager.grids[0]
                        overlay.setGridProduct(g.product, g.type, g.maxLod,
                                               g.urlInfo, g.urlData)
                    }
                    overlay.test()
                }
            }

            Flow {
                id: bottomButtonPanel
                anchors {
                    bottom: testBtn.top
                    horizontalCenter: parent.horizontalCenter
                    bottomMargin: 6
                }
                // Natural single-row width: grid buttons (64 px) + layer buttons
                // (54 px) + spacing between every button (4 px each gap).
                // Capped at the map width so Flow wraps on very narrow windows.
                width: Math.min(
                    gridManager.grids.length   * 64 +
                    layerManager.layers.length * 54 +
                    Math.max(0, gridManager.grids.length + layerManager.layers.length - 1) * 4,
                    parent.width)
                spacing: 4
                layoutDirection: Qt.LeftToRight

                // Grid buttons – from grids.json, pill-shaped teal with orange border
                Repeater {
                    id: gridRepeater
                    model: gridManager.grids

                    delegate: Button {
                        required property var modelData
                        required property int index

                        width: 64; height: 30
                        text: modelData.name

                        property bool gridActive: false
                        highlighted: gridActive

                        background: Rectangle {
                            color: parent.highlighted ? "#00897b" : "#26a69a"
                            radius: height / 2
                            border.color: "#ff8c00"
                            border.width: 2
                        }
                        contentItem: Text {
                            text: parent.text
                            font.pixelSize: 11
                            color: "white"
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }

                        onClicked: {
                            gridActive = !gridActive
                            if (gridActive) {
                                gridManager.enableGrid(index)
                            } else {
                                gridManager.disableGrid(index)
                                overlay.visible = false
                            }
                        }
                    }
                }

                // Layer buttons – from layers.json, rounded rect with purple border
                Repeater {
                    id: layerRepeater
                    model: layerManager.layers

                    delegate: Button {
                        required property var modelData
                        required property int index

                        width: 54; height: 36
                        text: modelData.name

                        property bool layerActive: false
                        highlighted: layerActive

                        background: Rectangle {
                            color: parent.highlighted ? "#ede7f6" : "#f0f0f0"
                            radius: 4
                            border.color: "#9c27b0"
                            border.width: 2
                        }

                        onClicked: {
                            layerActive = !layerActive
                            if (layerActive) {
                                layerManager.enableLayer(index)
                            } else {
                                layerManager.disableLayer(index)
                                var wo = weatherOverlays.itemAt(index)
                                if (wo) { wo.visible = false; wo.urlTemplate = ""; wo.tiles = [] }
                            }
                        }
                    }
                }
            }

            // ---------------------------------------------------------------
            // Tile-boundary grid toggle – bottom right
            // ---------------------------------------------------------------
            Button {
                id: tileGridBtn
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

        // ── Log pane ──────────────────────────────────────────────────────────
        Rectangle {
            id: logPane
            SplitView.preferredHeight: 140
            SplitView.minimumHeight: 60
            color: "#1a1a1a"

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                // Header bar
                Rectangle {
                    Layout.fillWidth: true
                    height: 28
                    color: "#252525"

                    Text {
                        anchors { left: parent.left; leftMargin: 10; verticalCenter: parent.verticalCenter }
                        text: "Log"
                        color: "#aaaaaa"
                        font.pixelSize: 12
                        font.bold: true
                    }

                    Button {
                        anchors { right: parent.right; rightMargin: 6; verticalCenter: parent.verticalCenter }
                        text: "Clear"
                        height: 22
                        font.pixelSize: 11
                        onClicked: appLogger.clear()
                    }
                }

                // Scrolling log text area
                ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    ScrollBar.vertical.policy: ScrollBar.AlwaysOn
                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

                    TextArea {
                        id: logArea
                        readOnly: true
                        selectByMouse: true
                        textFormat: TextEdit.RichText
                        text: appLogger.text
                        color: "#cccccc"
                        font.family: "monospace"
                        font.pixelSize: 12
                        wrapMode: TextArea.WrapAnywhere
                        background: Rectangle { color: "#1a1a1a" }
                        padding: 6
                        leftPadding: 10

                        // Auto-scroll to newest entry
                        onTextChanged: cursorPosition = length
                    }
                }
            }
        }
    }

    // ── GridManager signal handlers ──────────────────────────────────────────
    Connections {
        target: gridManager

        function onGridReady(index, endpoint) {
            appLogger.append("Grid " + index + " ready: " + endpoint)
            overlay.endpoint = endpoint
            // Tell the overlay which product and URLs are active
            var grid = gridManager.grids[index]
            overlay.setGridProduct(grid.product, grid.type, grid.maxLod,
                                   grid.urlInfo, grid.urlData)
            overlay.drawTile(0, 0, 0)
            updateOverlayTiles()
            overlay.visible = true
            var btn = gridRepeater.itemAt(index)
            if (btn) btn.gridActive = true
        }

        function onGridError(index, errorMessage) {
            appLogger.append("Grid " + index + " error: " + errorMessage)
            overlay.visible = false
            var btn = gridRepeater.itemAt(index)
            if (btn) btn.gridActive = false
        }
    }

    // ── LayerManager signal handlers ─────────────────────────────────────────
    Connections {
        target: layerManager

        function onLayerReady(index, tileUrlTemplate) {
            appLogger.append("Layer " + index + " ready: " + tileUrlTemplate)
            var wo = weatherOverlays.itemAt(index)
            if (wo) {
                wo.urlTemplate = tileUrlTemplate
                wo.visible = true
                wo.refresh()
            }
        }

        function onLayerError(index, errorMessage) {
            appLogger.append("Layer " + index + " error: " + errorMessage)
            var wo = weatherOverlays.itemAt(index)
            if (wo) { wo.visible = false; wo.urlTemplate = ""; wo.tiles = [] }
            var btn = layerRepeater.itemAt(index)
            if (btn) btn.layerActive = false
        }
    }
}
