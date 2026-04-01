import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs as Dialogs
import QtLocation
import QtPositioning
import MapApp

ApplicationWindow {
    id: root
    visible: true
    width: 1280
    height: 800
    title: {
        var base = "LanDen Labs - Map Viewer  v" + appVersion
        if (appSettings.userKeyActive)
            return base + "  [licensed]"
        var d = appSettings.daysRemaining
        if (d < 0)
            return base + "  [expired]"
        return base + "  [" + d + " day" + (d !== 1 ? "s" : "") + " left]"
    }

    onClosing: appSettings.setLastCenter(map.center.latitude, map.center.longitude)

    // ── Push-pin state ───────────────────────────────────────────────────────
    property string activePinColor: ""    // "" = no active pin color
    property int    currentPinIndex: -1  // index in pinModel of last placed pin

    ListModel { id: pinModel }

    function savePins() {
        var arr = []
        for (var i = 0; i < pinModel.count; i++) {
            var p = pinModel.get(i)
            arr.push({ name: p.name, lat: p.lat, lon: p.lon, pinColor: p.pinColor })
        }
        appSettings.setMapPins(JSON.stringify(arr))
    }

    function loadPins() {
        pinModel.clear()
        currentPinIndex = -1
        try {
            var data = JSON.parse(appSettings.mapPins)
            if (Array.isArray(data)) {
                for (var i = 0; i < data.length; i++) {
                    var e = data[i]
                    pinModel.append({
                        name:     e.name     || "",
                        lat:      e.lat      || 0,
                        lon:      e.lon      || 0,
                        pinColor: e.pinColor || "#cc3333"
                    })
                }
            }
        } catch(err) {
            appLogger.append("Pins load error: " + err)
        }
    }

    // When the active color changes, update the most recently placed pin
    onActivePinColorChanged: {
        if (activePinColor !== "" &&
                currentPinIndex >= 0 && currentPinIndex < pinModel.count) {
            pinModel.setProperty(currentPinIndex, "pinColor", activePinColor)
            savePins()
        }
    }

    Component.onCompleted: loadPins()

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
                    // Tile URL is read from QSettings once at startup.
                    // Change it in the About › License page; restart to apply.
                    PluginParameter {
                        name: "osm.mapping.custom.host"
                        value: appSettings.tileUrl
                    }
                    PluginParameter {
                        name: "osm.mapping.providersrepository.disabled"
                        value: "true"
                    }
                }

                // Initial view: restored from last session (default San Francisco)
                center: QtPositioning.coordinate(appSettings.lastCenterLat,
                                                 appSettings.lastCenterLon)
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
                    onActiveChanged: {
                        if (active) searchBar.searchExpanded = false
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
                // Map pins placed by the search bar
                // -------------------------------------------------------
                MapItemView {
                    model: pinModel
                    delegate: MapQuickItem {
                        required property int    index
                        required property string name
                        required property real   lat
                        required property real   lon
                        required property string pinColor

                        coordinate: QtPositioning.coordinate(lat, lon)
                        anchorPoint.x: 12
                        anchorPoint.y: 30
                        z: 10

                        sourceItem: Item {
                            id: pinSourceItem
                            width: 24
                            height: 32

                            property string pinColorVal: pinColor
                            property string pinNameVal:  name
                            property real   pinLatVal:   lat
                            property real   pinLonVal:   lon
                            property int    pinIdxVal:   index

                            Canvas {
                                id: pinCanvas
                                anchors.fill: parent
                                property string fillColor: parent.pinColorVal
                                onFillColorChanged: requestPaint()
                                Component.onCompleted: requestPaint()
                                onPaint: {
                                    var ctx = getContext("2d")
                                    ctx.clearRect(0, 0, width, height)
                                    var cx = width / 2
                                    var r  = width * 0.44
                                    ctx.fillStyle   = fillColor
                                    ctx.strokeStyle = Qt.darker(fillColor, 1.5).toString()
                                    ctx.lineWidth   = 1.5
                                    ctx.beginPath()
                                    ctx.moveTo(cx, height - 2)
                                    ctx.bezierCurveTo(cx - r * 0.3, height * 0.65,
                                                      cx - r,        r * 1.2,
                                                      cx - r,        r)
                                    ctx.arc(cx, r, r, Math.PI, 0, false)
                                    ctx.bezierCurveTo(cx + r,        r * 1.2,
                                                      cx + r * 0.3, height * 0.65,
                                                      cx, height - 2)
                                    ctx.closePath()
                                    ctx.fill()
                                    ctx.stroke()
                                    ctx.fillStyle = "rgba(255,255,255,0.45)"
                                    ctx.beginPath()
                                    ctx.arc(cx, r, r * 0.35, 0, 2 * Math.PI)
                                    ctx.fill()
                                }
                            }

                            MouseArea {
                                id: pinHoverArea
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: function(mouse) {
                                    var gp = mapToGlobal(mouse.x, mouse.y)
                                    var pp = mapPane.mapFromGlobal(gp.x, gp.y)
                                    pinRemovePopup.targetIndex = parent.pinIdxVal
                                    pinRemovePopup.x = pp.x - pinRemovePopup.width / 2
                                    pinRemovePopup.y = pp.y - pinRemovePopup.height - 6
                                    pinRemovePopup.open()
                                }
                            }

                            ToolTip {
                                visible: pinHoverArea.containsMouse
                                text: parent.pinNameVal
                                      + "\nLat: " + parent.pinLatVal.toFixed(5)
                                      + "  Lon: " + parent.pinLonVal.toFixed(5)
                                delay: 400
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
                    opacity: 0.75
                    visible: false
                    // Apply any search paths saved from a previous session.
                    Component.onCompleted: overlay.reloadPalettes(appSettings.searchPaths)
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
            // About / Settings dialog
            // ---------------------------------------------------------------
            Dialog {
                id: aboutDialog
                title: "LanDen Labs - Map Viewer"
                modal: true
                anchors.centerIn: parent
                width: 680
                height: 480
                padding: 0
                standardButtons: Dialog.Close

                property string currentPage: "about"
                onOpened: currentPage = "about"

                // Folder picker used by the Files/Paths/Cache page
                Dialogs.FolderDialog {
                    id: folderPickerDialog
                    title: "Add Search Path"
                    onAccepted: {
                        var path = selectedFolder.toString().replace(/^file:\/\//, "")
                        var paths = appSettings.searchPaths.slice()
                        paths.push(path)
                        appSettings.setSearchPaths(paths)
                    }
                }

                RowLayout {
                    anchors.fill: parent
                    spacing: 0

                    // ── Left navigation ───────────────────────────────────
                    Rectangle {
                        Layout.preferredWidth: 160
                        Layout.fillHeight: true
                        color: "#f5f5f5"

                        // Right border line
                        Rectangle {
                            anchors { top: parent.top; bottom: parent.bottom; right: parent.right }
                            width: 1
                            color: "#e0e0e0"
                        }

                        Column {
                            width: parent.width
                            anchors { top: parent.top; topMargin: 8 }

                            Repeater {
                                model: ListModel {
                                    ListElement { navLabel: "About";             navKey: "about"   }
                                    ListElement { navLabel: "Files/Paths/Cache"; navKey: "files"   }
                                    ListElement { navLabel: "License";           navKey: "license" }
                                }

                                delegate: Rectangle {
                                    required property string navLabel
                                    required property string navKey
                                    width: 160
                                    height: 38
                                    color: aboutDialog.currentPage === navKey ? "#dce8fb" : "transparent"

                                    Rectangle {
                                        visible: aboutDialog.currentPage === navKey
                                        width: 3; height: parent.height
                                        anchors.left: parent.left
                                        color: "#1a73e8"
                                    }

                                    Text {
                                        anchors {
                                            left: parent.left; leftMargin: 14
                                            verticalCenter: parent.verticalCenter
                                        }
                                        text: navLabel
                                        font.pixelSize: 13
                                        color: aboutDialog.currentPage === navKey ? "#1a73e8" : "#333333"
                                        font.bold: aboutDialog.currentPage === navKey
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: aboutDialog.currentPage = navKey
                                    }
                                }
                            }
                        }
                    }

                    // ── Right content ─────────────────────────────────────
                    StackLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        currentIndex: aboutDialog.currentPage === "about"   ? 0 :
                                      aboutDialog.currentPage === "files"   ? 1 : 2

                        // ── About ─────────────────────────────────────────
                        Item {
                            Column {
                                anchors {
                                    top: parent.top; topMargin: 20
                                    left: parent.left; leftMargin: 20
                                    right: parent.right; rightMargin: 20
                                }
                                spacing: 10

                                Rectangle {
                                    width: 80; height: 80
                                    color: "#2c3e50"
                                    radius: 8

                                    Image {
                                        anchors.fill: parent
                                        anchors.margins: 6
                                        source: "qrc:/images/landenlabs.png"
                                        fillMode: Image.PreserveAspectFit
                                        smooth: true
                                        onStatusChanged: {
                                            if (status === Image.Error)
                                                appLogger.append("Logo load failed: " + source)
                                            else if (status === Image.Ready)
                                                appLogger.append("Logo loaded OK: " + source)
                                        }
                                    }
                                }
                                Text {
                                    width: parent.width
                                    wrapMode: Text.WordWrap
                                    font.pixelSize: 15
                                    font.bold: true
                                    text: "LanDen Labs - Map Viewer"
                                }
                                Text {
                                    width: parent.width
                                    wrapMode: Text.WordWrap
                                    font.pixelSize: 13
                                    text: "Interactive map viewer built with Qt 6 and Qt Location.\n" +
                                          "Displays OpenStreetMap tiles with pan and pinch-to-zoom.\n" +
                                          "Supports a floating-point grid overlay rendered via an " +
                                          "OpenGL/RHI fragment shader with a palette colormap."
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

                        // ── Files / Paths / Cache ─────────────────────────
                        Item {
                            Column {
                                id: cacheInfoCol
                                anchors {
                                    top: parent.top; topMargin: 20
                                    left: parent.left; leftMargin: 20
                                    right: parent.right; rightMargin: 20
                                }
                                spacing: 8

                                Text {
                                    width: parent.width
                                    font.pixelSize: 13
                                    font.bold: true
                                    text: "Tile Cache"
                                }
                                Text {
                                    width: parent.width
                                    wrapMode: Text.WordWrap
                                    font.pixelSize: 12
                                    color: "#444444"
                                    text: "If old watermarked tiles appear after changing the tile " +
                                          "provider, clear the disk cache:"
                                }
                                RowLayout {
                                    width: parent.width
                                    spacing: 6
                                    Text {
                                        Layout.fillWidth: true
                                        wrapMode: Text.WrapAnywhere
                                        font.pixelSize: 12
                                        font.family: "monospace"
                                        color: "#222222"
                                        text: "~/Library/Caches/QtLocation/5.8/tiles/osm/"
                                    }
                                    Button {
                                        text: "Open"
                                        Layout.preferredHeight: 26
                                        font.pixelSize: 11
                                        onClicked: Qt.openUrlExternally(
                                            "file://" + homePath +
                                            "/Library/Caches/QtLocation/5.8/tiles/osm/")
                                    }
                                }
                                Rectangle { width: parent.width; height: 1; color: "#cccccc" }
                                Text {
                                    width: parent.width
                                    font.pixelSize: 13
                                    font.bold: true
                                    text: "Search Paths"
                                }
                                Text {
                                    width: parent.width
                                    wrapMode: Text.WordWrap
                                    font.pixelSize: 12
                                    color: "#444444"
                                    text: "Directories to scan for layers.json, grids.json, " +
                                          "and palettes.json. External entries replace built-in " +
                                          "entries with the same name."
                                }
                            }

                            Rectangle {
                                id: fileListBox
                                anchors {
                                    top: cacheInfoCol.bottom; topMargin: 8
                                    left: parent.left; leftMargin: 20
                                    right: parent.right; rightMargin: 20
                                    bottom: fileListButtons.top; bottomMargin: 8
                                }
                                color: "white"
                                border.color: "#cccccc"
                                border.width: 1
                                radius: 3
                                clip: true

                                ListView {
                                    id: fileListView
                                    anchors.fill: parent
                                    anchors.margins: 2
                                    clip: true
                                    model: appSettings.searchPaths

                                    delegate: Rectangle {
                                        width: fileListView.width
                                        height: 26
                                        color: fileListView.currentIndex === index
                                               ? "#e3f2fd" : "transparent"

                                        Text {
                                            anchors {
                                                left: parent.left; leftMargin: 8
                                                verticalCenter: parent.verticalCenter
                                            }
                                            text: modelData
                                            font.pixelSize: 12
                                            font.family: "monospace"
                                            elide: Text.ElideLeft
                                            width: parent.width - 16
                                        }

                                        MouseArea {
                                            anchors.fill: parent
                                            onClicked: fileListView.currentIndex = index
                                        }
                                    }
                                }
                            }

                            Row {
                                id: fileListButtons
                                anchors {
                                    bottom: parent.bottom; bottomMargin: 12
                                    left: parent.left; leftMargin: 20
                                }
                                spacing: 6

                                Button {
                                    text: "Add…"
                                    height: 26
                                    font.pixelSize: 11
                                    onClicked: folderPickerDialog.open()
                                }
                                Button {
                                    text: "Remove"
                                    height: 26
                                    font.pixelSize: 11
                                    enabled: fileListView.currentIndex >= 0 &&
                                             fileListView.count > 0
                                    onClicked: {
                                        var idx = fileListView.currentIndex
                                        if (idx >= 0) {
                                            var paths = appSettings.searchPaths.slice()
                                            paths.splice(idx, 1)
                                            appSettings.setSearchPaths(paths)
                                        }
                                    }
                                }
                            }
                        }

                        // ── License / API Keys ────────────────────────────
                        ScrollView {
                            clip: true
                            contentWidth: availableWidth

                            Column {
                                width: parent.width
                                topPadding: 20
                                bottomPadding: 20
                                leftPadding: 20
                                rightPadding: 20
                                spacing: 10

                                // ── Weather Data ──────────────────────────
                                Text {
                                    width: parent.width
                                    font.pixelSize: 13
                                    font.bold: true
                                    text: "Weather Data API Key"
                                }
                                Text {
                                    width: parent.width
                                    wrapMode: Text.WordWrap
                                    font.pixelSize: 12
                                    color: "#444444"
                                    text: "Your own API key overrides the built-in key and never " +
                                          "expires. Leave blank to use the built-in key while valid."
                                }
                                TextField {
                                    id: sunApiKeyField
                                    width: parent.width
                                    text: appSettings.sunApiKey
                                    placeholderText: "Enter SUN API key…"
                                    font.pixelSize: 12
                                    font.family: "monospace"
                                    echoMode: TextInput.Password
                                }
                                Row {
                                    spacing: 6
                                    Button {
                                        text: "Show"
                                        height: 26
                                        font.pixelSize: 11
                                        checkable: true
                                        onCheckedChanged: {
                                            sunApiKeyField.echoMode = checked
                                                ? TextInput.Normal
                                                : TextInput.Password
                                        }
                                    }
                                    Button {
                                        text: "Save"
                                        height: 26
                                        font.pixelSize: 11
                                        enabled: sunApiKeyField.text.trim() !== "" &&
                                                 sunApiKeyField.text !== appSettings.sunApiKey
                                        onClicked: {
                                            appSettings.setSunApiKey(sunApiKeyField.text.trim())
                                            appLogger.append("Weather API key saved — re-enable layers to apply")
                                        }
                                    }
                                    Button {
                                        text: "Clear"
                                        height: 26
                                        font.pixelSize: 11
                                        enabled: appSettings.userKeyActive
                                        onClicked: {
                                            appSettings.setSunApiKey("")
                                            sunApiKeyField.text = ""
                                            appLogger.append("Weather API key cleared — using built-in key")
                                        }
                                    }
                                }
                                Text {
                                    width: parent.width
                                    font.pixelSize: 11
                                    color: appSettings.userKeyActive  ? "#2e7d32" :
                                           appSettings.daysRemaining < 0 ? "#c62828" : "#616161"
                                    text: appSettings.userKeyActive
                                          ? "User key active — unlimited access"
                                          : (appSettings.daysRemaining < 0
                                             ? "Built-in key expired — enter your own key above"
                                             : "Built-in key — " + appSettings.daysRemaining +
                                               " day" + (appSettings.daysRemaining !== 1 ? "s" : "") +
                                               " remaining")
                                }

                                Rectangle { width: parent.width; height: 1; color: "#cccccc" }

                                // ── Base Map ──────────────────────────────
                                Text {
                                    width: parent.width
                                    font.pixelSize: 13
                                    font.bold: true
                                    text: "Base Map Tile Server"
                                }
                                Text {
                                    width: parent.width
                                    wrapMode: Text.WordWrap
                                    font.pixelSize: 12
                                    color: "#444444"
                                    text: "Full tile URL template. Use %z, %x, %y for zoom and " +
                                          "tile coordinates. Changes take effect after restarting."
                                }
                                TextField {
                                    id: tileUrlField
                                    width: parent.width
                                    text: appSettings.tileUrl
                                    font.pixelSize: 11
                                    font.family: "monospace"
                                    placeholderText: "https://tile.openstreetmap.org/%z/%x/%y.png"
                                }
                                Row {
                                    spacing: 6
                                    Button {
                                        text: "Save"
                                        height: 26
                                        font.pixelSize: 11
                                        enabled: tileUrlField.text.trim() !== "" &&
                                                 tileUrlField.text !== appSettings.tileUrl
                                        onClicked: {
                                            appSettings.setTileUrl(tileUrlField.text.trim())
                                            appLogger.append("Tile URL saved — restart to apply")
                                        }
                                    }
                                    Button {
                                        text: "Reset"
                                        height: 26
                                        font.pixelSize: 11
                                        onClicked: {
                                            tileUrlField.text = appSettings.defaultTileUrl()
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // ---------------------------------------------------------------
            // Top-left toolbar: About / Tile-grid / Test
            // ---------------------------------------------------------------
            Row {
                anchors {
                    top: parent.top
                    left: parent.left
                    margins: 10
                }
                spacing: 6

                // About – green when licensed, red when expired
                RoundButton {
                    text: "?"
                    width: 36; height: 36
                    background: Rectangle {
                        radius: width / 2
                        color: appSettings.userKeyActive      ? "#a5d6a7" :
                               appSettings.daysRemaining < 0  ? "#ef9a9a" : "#e0e0e0"
                        border.color: Qt.darker(color, 1.3)
                        border.width: 1
                    }
                    ToolTip.visible: hovered
                    ToolTip.delay:   600
                    ToolTip.text:    "About / Settings"
                    onClicked: aboutDialog.open()
                }

                // Tile-grid toggle – blue tint when active
                RoundButton {
                    width: 36; height: 36
                    background: Rectangle {
                        radius: width / 2
                        color: tileGridCanvas.visible ? "#bbdefb" : "#e0e0e0"
                        border.color: Qt.darker(color, 1.3)
                        border.width: 1
                    }
                    Image {
                        anchors.centerIn: parent
                        width: parent.width - 8; height: parent.height - 8
                        source: "qrc:/images/grid128.png"
                        fillMode: Image.PreserveAspectFit
                        smooth: true
                    }
                    ToolTip.visible: hovered
                    ToolTip.delay:   600
                    ToolTip.text:    "Toggle tile grid overlay"
                    onClicked: tileGridCanvas.visible = !tileGridCanvas.visible
                }

                // Test – load first grid entry and call overlay.test()
                RoundButton {
                    width: 36; height: 36
                    background: Rectangle {
                        radius: width / 2
                        color: "#e0e0e0"
                        border.color: Qt.darker(color, 1.3)
                        border.width: 1
                    }
                    Image {
                        anchors.centerIn: parent
                        width: parent.width - 8; height: parent.height - 8
                        source: "qrc:/images/test.jpeg"
                        fillMode: Image.PreserveAspectFit
                        smooth: true
                    }
                    ToolTip.visible: hovered
                    ToolTip.delay:   600
                    ToolTip.text:    "Load test grid overlay"
                    onClicked: {
                        if (gridManager.grids.length > 0) {
                            var g = gridManager.grids[0]
                            overlay.setGridProduct(g.product, g.type, g.maxLod,
                                                   g.urlInfo, g.urlData, g.paletteName)
                        }
                        overlay.test()
                    }
                }
            }

            // ---------------------------------------------------------------
            // Pin color selector popup (opened by pin icon in search bar)
            // ---------------------------------------------------------------
            Popup {
                id: pinColorPopup
                width: 150
                padding: 4
                modal: false
                closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

                background: Rectangle {
                    color: "white"
                    radius: 6
                    border.color: "#cccccc"
                    border.width: 1
                    layer.enabled: true
                    layer.effect: null
                }

                Column {
                    spacing: 2
                    width: pinColorPopup.width - 8

                    Repeater {
                        model: ListModel {
                            ListElement { clr: "";          lbl: "None"   }
                            ListElement { clr: "#cc3333";   lbl: "Red"    }
                            ListElement { clr: "#2a9d2a";   lbl: "Green"  }
                            ListElement { clr: "#1a73e8";   lbl: "Blue"   }
                            ListElement { clr: "#e8891a";   lbl: "Orange" }
                            ListElement { clr: "#9c27b0";   lbl: "Purple" }
                            ListElement { clr: "#d4a017";   lbl: "Yellow" }
                            ListElement { clr: "#00838f";   lbl: "Cyan"   }
                            ListElement { clr: "__reset__"; lbl: "Reset"  }
                        }

                        delegate: Rectangle {
                            required property string clr
                            required property string lbl
                            width: parent.width
                            height: 28
                            radius: 4
                            color: menuArea.containsMouse ? "#e8e8e8" : "transparent"

                            Row {
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.left: parent.left
                                anchors.leftMargin: 6
                                spacing: 8

                                Rectangle {
                                    width: 16; height: 16; radius: 8
                                    anchors.verticalCenter: parent.verticalCenter
                                    color: (clr !== "" && clr !== "__reset__") ? clr : "transparent"
                                    border.color: (clr !== "" && clr !== "__reset__")
                                                  ? Qt.darker(clr, 1.4).toString() : "transparent"
                                    border.width: 1
                                }

                                Text {
                                    text: lbl
                                    font.pixelSize: 13
                                    anchors.verticalCenter: parent.verticalCenter
                                    color: clr === "__reset__" ? "#cc3333" : "#333333"
                                }
                            }

                            MouseArea {
                                id: menuArea
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: {
                                    pinColorPopup.close()
                                    if (clr === "__reset__") {
                                        pinModel.clear()
                                        root.currentPinIndex = -1
                                        root.activePinColor = ""
                                        root.savePins()
                                    } else {
                                        root.activePinColor = clr
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // ---------------------------------------------------------------
            // Pin remove popup (opened by clicking a map pin)
            // ---------------------------------------------------------------
            Popup {
                id: pinRemovePopup
                property int targetIndex: -1
                width: 110
                height: 36
                padding: 0
                modal: false
                closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

                background: Rectangle {
                    color: "white"
                    radius: 6
                    border.color: "#cccccc"
                    border.width: 1
                    layer.enabled: true
                    layer.effect: null
                }

                Button {
                    width: parent.width
                    height: parent.height
                    text: "Remove pin"
                    font.pixelSize: 13
                    background: Rectangle {
                        color: parent.hovered ? "#ffe0e0" : "transparent"
                        radius: 6
                    }
                    contentItem: Text {
                        text: parent.text
                        font: parent.font
                        color: "#cc3333"
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    onClicked: {
                        var idx = pinRemovePopup.targetIndex
                        if (idx >= 0 && idx < pinModel.count) {
                            if (root.currentPinIndex === idx)
                                root.currentPinIndex = -1
                            else if (root.currentPinIndex > idx)
                                root.currentPinIndex -= 1
                            pinModel.remove(idx)
                            root.savePins()
                        }
                        pinRemovePopup.close()
                    }
                }
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
                // Collapses to just the two icons when the map is panned
                property bool searchExpanded: true
                width: searchExpanded ? Math.min(520, parent.width - 200) : 80
                height: 36

                Behavior on width { NumberAnimation { duration: 180; easing.type: Easing.InOutQuad } }

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
                        appSettings.setLastCenter(lat, lon)
                        searchState = "found"
                        searchField.text = results[0].display_name
                        // Place a new pin if a color is active
                        if (root.activePinColor !== "") {
                            pinModel.append({
                                name:     results[0].display_name,
                                lat:      lat,
                                lon:      lon,
                                pinColor: root.activePinColor
                            })
                            root.currentPinIndex = pinModel.count - 1
                            root.savePins()
                        }
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

                    // Tap anywhere on the collapsed bar to expand
                    MouseArea {
                        anchors.fill: parent
                        enabled: !searchBar.searchExpanded
                        cursorShape: Qt.PointingHandCursor
                        onClicked: searchBar.searchExpanded = true
                    }
                }

                // Push-pin icon – opens the color picker dropdown
                Item {
                    id: pinBtn
                    anchors { left: parent.left; leftMargin: 8; verticalCenter: parent.verticalCenter }
                    width: 22; height: 22

                    Canvas {
                        id: pinBtnCanvas
                        anchors.fill: parent
                        property string fillColor: root.activePinColor !== "" ? root.activePinColor : "#888888"
                        onFillColorChanged: requestPaint()
                        Component.onCompleted: requestPaint()
                        onPaint: {
                            var ctx = getContext("2d")
                            ctx.clearRect(0, 0, width, height)
                            var cx = width / 2
                            var r  = width * 0.38
                            var col = fillColor
                            ctx.fillStyle   = col
                            ctx.strokeStyle = Qt.darker(col, 1.4).toString()
                            ctx.lineWidth   = 1
                            ctx.beginPath()
                            ctx.moveTo(cx, height - 1)
                            ctx.bezierCurveTo(cx - r * 0.3, height * 0.65,
                                              cx - r,        r * 1.2,
                                              cx - r,        r)
                            ctx.arc(cx, r, r, Math.PI, 0, false)
                            ctx.bezierCurveTo(cx + r,        r * 1.2,
                                              cx + r * 0.3, height * 0.65,
                                              cx, height - 1)
                            ctx.closePath()
                            ctx.fill()
                            ctx.stroke()
                            ctx.fillStyle = "rgba(255,255,255,0.45)"
                            ctx.beginPath()
                            ctx.arc(cx, r, r * 0.35, 0, 2 * Math.PI)
                            ctx.fill()
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            searchBar.searchExpanded = true
                            pinColorPopup.x = searchBar.x + pinBtn.x
                            pinColorPopup.y = searchBar.y + searchBar.height + 4
                            pinColorPopup.open()
                        }
                    }
                }

                Item {
                    id: searchIcon
                    anchors { left: pinBtn.right; leftMargin: 4; verticalCenter: parent.verticalCenter }
                    width: 22; height: 22

                    // Canvas-drawn magnifier – precise pixel control, no glyph padding
                    Canvas {
                        anchors.fill: parent
                        visible: searchBar.searchState !== "searching"
                        Component.onCompleted: requestPaint()
                        onPaint: {
                            var ctx = getContext("2d")
                            ctx.clearRect(0, 0, width, height)
                            var r  = width * 0.30
                            var cx = width  * 0.40
                            var cy = height * 0.40
                            ctx.strokeStyle = "#666666"
                            ctx.lineWidth   = width * 0.14
                            ctx.lineCap     = "round"
                            // Glass circle
                            ctx.beginPath()
                            ctx.arc(cx, cy, r, 0, 2 * Math.PI)
                            ctx.stroke()
                            // Handle
                            ctx.beginPath()
                            ctx.moveTo(cx + r * 0.72, cy + r * 0.72)
                            ctx.lineTo(width * 0.88, height * 0.88)
                            ctx.stroke()
                        }
                    }

                    // Spinner shown while a search is in flight
                    Text {
                        anchors.centerIn: parent
                        visible: searchBar.searchState === "searching"
                        text: "…"
                        font.pixelSize: 18
                        color: "#666666"
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            searchBar.searchExpanded = true
                            searchField.forceActiveFocus()
                        }
                    }
                }

                TextField {
                    id: searchField
                    visible: searchBar.searchExpanded
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
                    visible: searchBar.searchExpanded && searchField.text !== ""
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
            Flow {
                id: bottomButtonPanel
                anchors {
                    bottom: parent.bottom
                    horizontalCenter: parent.horizontalCenter
                    bottomMargin: 10
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
                            color: parent.highlighted ? "#4caf50" : "white"
                            radius: height / 2
                            border.color: "#ff8c00"
                            border.width: 2
                        }
                        contentItem: Text {
                            text: parent.text
                            font.pixelSize: 11
                            color: parent.highlighted ? "white" : "#333333"
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }

                        ToolTip.visible: hovered && modelData.comment.length > 0
                        ToolTip.delay:   600
                        ToolTip.text:    modelData.comment

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
                            color: parent.highlighted ? "#4caf50" : "white"
                            radius: height / 2
                            border.color: "#9c27b0"
                            border.width: 2
                        }
                        contentItem: Text {
                            text: parent.text
                            font.pixelSize: 11
                            color: parent.highlighted ? "white" : "#333333"
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }

                        ToolTip.visible: hovered && modelData.comment.length > 0
                        ToolTip.delay:   600
                        ToolTip.text:    modelData.comment

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

    // ── AppSettings signal handlers ──────────────────────────────────────────
    Connections {
        target: appSettings
        function onSearchPathsChanged(paths) {
            // LayerManager and GridManager are reloaded in main.cpp via C++
            // connection; reload palettes here so the overlay updates too.
            overlay.reloadPalettes(paths)
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
                                   grid.urlInfo, grid.urlData, grid.paletteName)
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
