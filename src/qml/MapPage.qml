// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
// SPDX-FileCopyrightText: 2022 Harald Sitter <sitter@kde.org>

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.kde.kirigami.delegates as KD
import org.kde.coreaddons as KCoreAddons
import QtQuick.Shapes

import org.kde.filelight

Kirigami.Page {
    id: page

    property url url: RadialMap.rootUrl
    readonly property bool treemap: Config.mapStyle === Filelight.Treemap
    property QQC2.Menu contextMenu

    ContextMenuContext{
        id: contextMenuContext
    }

    enabled: !contextMenuContext.deleting
    topPadding: 0
    leftPadding: 0
    rightPadding: 0
    bottomPadding: 0

    Connections {
        target: contextMenuContext
        function onDeleteFileFailed(reason) {
            showPassiveNotification(reason);
        }
    }

    footer: QQC2.ToolBar {
        height: footerLabel.implicitHeight + topPadding + bottomPadding
        visible: shapeItem.visible
        contentItem: QQC2.Label {
            id: footerLabel
            verticalAlignment: Text.AlignVCenter
            text: (RadialMap.numberOfChildren === 0) ?
            i18nc("@info:status", "No files shown.") :
            i18ncp("@info:status", "1 file shown", "%1 files shown", RadialMap.numberOfChildren)
        }
    }

    Kirigami.Action {
        id: goToOverviewAction
        enabled: page.state === ""
        icon.name: "go-home"
        text: i18nc("@action", "Go to Overview")
        onTriggered: pageStack.currentIndex = 0
    }

    Kirigami.Action {
        id: goUpAction
        enabled: page.state === "" && MainContext.upEnabled
        icon.name: "go-parent-folder"
        text: i18nc("@action", "Up")
        onTriggered: appWindow.slotUp()
        shortcut: "Alt+Up"
    }

    Kirigami.Action {
        id: rescanAction
        enabled: page.state !== "scanning"
        icon.name: "view-refresh"
        text: i18nc("@action", "Rescan")
        onTriggered: appWindow.rescan()
        shortcut: StandardKey.Refresh
    }

    Kirigami.Action {
        id: stopAction
        enabled: page.state === "scanning"
        icon.name: "process-stop"
        text: i18nc("@action", "Stop")
        onTriggered: appWindow.closeURL()
    }

    Kirigami.Action {
        id: zoomInAction
        enabled: page.state === ""
        icon.name: "zoom-in"
        text: i18nc("@action", "Zoom In")
        displayHint: Kirigami.DisplayHint.AlwaysHide
        onTriggered: RadialMap.zoomIn()
        shortcut: StandardKey.ZoomIn
    }

    Kirigami.Action {
        id: zoomOutAction
        enabled: page.state === ""
        icon.name: "zoom-out"
        text: i18nc("@action", "Zoom Out")
        displayHint: Kirigami.DisplayHint.AlwaysHide
        onTriggered: RadialMap.zoomOut()
        shortcut: StandardKey.ZoomOut
    }

    actions: [
        goUpAction,
        goToOverviewAction,
        rescanAction,
        stopAction,
        zoomInAction,
        zoomOutAction,
        configureAction,
        helpAction,
        aboutAction
    ]

    Component {
        id: contextMenuComponent
        QQC2.Menu {
            id: contextMenu

            required property Segment segment

            title: segment.displayName()
            visible: !segment.isFilesGroup

            onAboutToShow: page.contextMenu = this
            onAboutToHide: page.contextMenu = null

            QQC2.MenuItem {
                action: Kirigami.Action {
                    icon.name: "document-open"
                    text: i18nc("@action Open file or directory from context menu", "Open")
                }
                onTriggered: Qt.openUrlExternally(contextMenu.segment.url())
            }
            QQC2.MenuItem {
                visible: contextMenu.segment.isFolder() && contextMenu.segment.url().toString().startsWith("file:")
                action: Kirigami.Action {
                    icon.name: "utilities-terminal"
                    text: i18nc("@action", "Open Terminal Here")
                    onTriggered: contextMenuContext.openTerminal(contextMenu.segment.url())
                }
            }
            QQC2.MenuItem {
                visible: contextMenu.segment.isFolder()
                action: Kirigami.Action {
                    icon.name: "zoom-in"
                    text: i18nc("@action focuses the filelight view on a given map segment", "Center Map Here")
                    onTriggered: {
                        MainContext.updateURL(contextMenu.segment.url())
                        MainContext.openUrl(contextMenu.segment.url())
                    }
                }
            }
            QQC2.MenuItem {
                visible: contextMenu.segment.isFolder()
                action: Kirigami.Action {
                    icon.name: "list-remove"
                    text: i18nc("@action", "Add to Do Not Scan List")
                    onTriggered: contextMenuContext.doNotScan(contextMenu.segment.url())
                }
            }
            QQC2.MenuItem {
                visible: contextMenu.segment.isFolder()
                action: Kirigami.Action {
                    icon.name: "view-refresh"
                    text: i18nc("@action rescan filelight map", "Rescan")
                    onTriggered: MainContext.rescanSingleDir(contextMenu.segment.url())
                }
            }
            QQC2.MenuItem {
                action: Kirigami.Action {
                    icon.name: "edit-copy"
                    text: i18nc("@action", "Copy to clipboard")
                    onTriggered: contextMenuContext.copyClipboard(contextMenu.segment.displayPath())
                }
            }
            QQC2.MenuSeparator {}
            QQC2.MenuItem {
                action: Kirigami.Action {
                    icon.name: "edit-delete"
                    text: i18nc("@action delete file or folder", "Delete")
                    onTriggered: contextMenuContext.deleteFileFromSegment(contextMenu.segment)
                }
            }
        }
    }

    property real mouseyX: -1
    property real mouseyY: -1
    property var hoveredSegment: undefined
    property bool hoveringListItem: false

    RowLayout {
        anchors.fill: parent
        spacing: 0
        visible: page.state === ""

        QQC2.ScrollView {
            visible: Config.showFoldersSidebar
            implicitWidth: Kirigami.Units.gridUnit * 10
            Layout.maximumWidth: Kirigami.Units.gridUnit * 22
            Layout.fillWidth: true
            Layout.fillHeight: true
            Kirigami.Theme.colorSet: Kirigami.Theme.View
            Kirigami.Theme.inherit: false

            // flush against both the toolbar and the window edge. without this we get a framed rectangle
            background: Rectangle {
	             color: Kirigami.Theme.backgroundColor
	        }
            Component.onCompleted: background.visible = true

            ListView {
                id: listview
                clip: true
                model: visible ? FileModel : undefined
                Component.onCompleted: currentIndex = -1
                reuseItems: true
                focus: true
                activeFocusOnTab: true
                keyNavigationEnabled: true
                keyNavigationWraps: true
                delegate: KD.SubtitleDelegate {
                    width: listview.width
                    icon.name: ROLE_IsFolder ? "folder" : "file" // TODO mimetype?
                    icon.width: Kirigami.Units.iconSizes.huge
                    text: model.display
                    subtitle: ROLE_HumanReadableSize
                    hoverEnabled: !page.contextMenu
                    highlighted: {
                        if (page.contextMenu) {
                            return (listview.currentIndex === index) && hoveringListItem;
                        } else {
                            if (hoveringListItem) {
                                return hovered
                            }
                            return hoveredSegment === ROLE_Segment
                        }
                    }
                    onHoveredChanged: {
                        if (hovered) {
                            listview.currentIndex = index;
                            hoveringListItem = true
                            hoveredSegment = ROLE_Segment
                        }
                    }
                    onClicked: {
                        if (ROLE_IsFolder) {
                            MainContext.updateURL(ROLE_URL)
                            MainContext.openUrl(ROLE_URL)
                        }
                    }

                    QQC2.Menu {
                        id: contextMenu

                        onAboutToShow: page.contextMenu = this
                        onAboutToHide: page.contextMenu = null

                        QQC2.MenuItem {
                            action: Kirigami.Action {
                                icon.name: "document-open"
                                text: i18nc("@action Open file or directory from context menu", "Open")
                            }
                            onTriggered: Qt.openUrlExternally(ROLE_URL)
                        }
                        QQC2.MenuItem {
                            visible: ROLE_IsFolder && ROLE_URL.toString().startsWith("file:")
                            action: Kirigami.Action {
                                icon.name: "utilities-terminal"
                                text: i18nc("@action", "Open Terminal Here")
                                onTriggered: contextMenuContext.openTerminal(ROLE_URL)
                            }
                        }
                        QQC2.MenuItem {
                            visible: ROLE_IsFolder
                            action: Kirigami.Action {
                                icon.name: "list-remove"
                                text: i18nc("@action", "Add to Do Not Scan List")
                                onTriggered: contextMenuContext.doNotScan(ROLE_URL)
                            }
                        }
                        QQC2.MenuItem {
                            action: Kirigami.Action {
                                icon.name: "edit-copy"
                                text: i18nc("@action", "Copy to clipboard")
                                onTriggered: contextMenuContext.copyClipboard(ROLE_URL)
                            }
                        }
                        QQC2.MenuSeparator {}
                        QQC2.MenuItem {
                            action: Kirigami.Action {
                                icon.name: "edit-delete"
                                text: i18nc("@action delete file or folder", "Delete")
                                onTriggered: contextMenuContext.deleteFile(FileModel.file(index))
                            }
                        }
                    }

                    TapHandler {
                        acceptedButtons: Qt.RightButton
                        onTapped: contextMenu.popup()
                    }
                }
            }
        }

        Kirigami.Separator {
            visible: Config.showFoldersSidebar
            Layout.fillHeight: true
        }

        Item {
            id: shapeItem
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: Kirigami.Units.gridUnit * 10
            Layout.minimumWidth: Kirigami.Units.gridUnit * 10
            Layout.margins: Kirigami.Units.gridUnit
            // Render shapes directly. An offscreen layer can clip the map with the
            // software backend at fractional display scales.

            property var zOrderedShapes: []
            // The item under the mouse, see the MouseArea below.
            property var hoveredItem: null

            // The treemap shares one tooltip between all of its tiles, there can be thousands of them.
            ShapeToolTip {
                parent: shapeItem
                mouseX: mouseyX
                mouseY: mouseyY
                readonly property var tile: page.treemap ? shapeItem.hoveredItem : null
                // Only computed for the tile under the mouse.
                text: tile ? (tile.isRoot ? shapeItem.rootTooltip() : shapeItem.segmentTooltip(tile.segment)) : ""
                visible: tile ? (tile.hovered && !contextMenu && !hoveringListItem && shapeItem.visible) : false
            }
            property bool hasShapes: zOrderedShapes.length > 0

            function objectToArray(object) {
                let newArray = [];
                for (let i in object) {
                    newArray.push(object[i]);
                }
                return newArray;
            }

            function updateZOrderedShapes() {
                let children = objectToArray(shapeItem.children)
                // Shapes of the radial map and rectangles of the treemap.
                // Children being destroyed may already be null.
                children = children.filter(child => child && child.segmentUuid !== undefined);
                children.sort((a, b) => b.z - a.z); // sort by level so the higher level object (the visible one) outscores ones it paints over
                zOrderedShapes = children
            }

            // The map consists of up to thousands of items, which are added one at a time.
            // Sorting all of them after every single one is far too slow, so only sort once they are all in.
            onChildrenChanged: Qt.callLater(updateZOrderedShapes)

            function tooltip({ path, size, isFolder, files, totalFiles, isRoot = false }) {
                let tips = [path, size]
                if (isFolder) {
                    const percent = Math.floor((100 * files) / totalFiles)
                    if (percent > 0) {
                        tips.push(i18ncp("Tooltip of folder, %1 is number of files", "%1 File (%2%)", "%1 Files (%2%)", files, percent))
                    } else {
                        tips.push(i18ncp("Tooltip of folder, %1 is number of files", "%1 File", "%1 Files", files))
                    }
                }
                if (isRoot) {
                    tips.push(i18nc("part of tooltip indicating that the item under the mouse is clickable", "Click to go up to parent directory"))
                }
                return tips.join("\n")
            }

            function segmentTooltip(segment) {
                return tooltip({
                    isFolder: segment.isFolder(),
                    path: segment.displayPath(),
                    size: segment.humanReadableSize(),
                    files: segment.files(),
                    totalFiles: RadialMap.numberOfChildren,
                })
            }

            function rootTooltip() {
                return tooltip({
                    isFolder: true,
                    path: RadialMap.displayPath,
                    size: RadialMap.overallSize,
                    files: RadialMap.numberOfChildren,
                    totalFiles: RadialMap.numberOfChildren,
                    isRoot: true,
                })
            }

            FontMetrics {
                id: treemapMetrics
            }

            function layoutTreemap() {
                if (!page.treemap || !RadialMap.valid) {
                    return
                }
                RadialMap.layoutTreemap(width, height,
                                        treemapMetrics.height + Kirigami.Units.smallSpacing * 2, // header
                                        Kirigami.Units.smallSpacing, // padding
                                        Kirigami.Units.smallSpacing / 2) // spacing
            }

            onWidthChanged: layoutTreemap()
            onHeightChanged: layoutTreemap()
            Connections {
                target: RadialMap
                function onSignatureChanged() {
                    shapeItem.layoutTreemap()
                }
            }
            Connections {
                target: page
                function onTreemapChanged() {
                    shapeItem.layoutTreemap()
                }
            }

            Instantiator {
                id: instantiator
                model: shapeItem.visible ? RadialMap.signature : undefined
                active: true
                asynchronous: true // arguable

                Instantiator {
                    id: levelInstantiator
                    // FIXME weird
                    Component.onCompleted: idx = index // lock index by breaking the binding
                    property int idx: index
                    readonly property real signatureRadius: {
                        if (shapeItem.width > shapeItem.height) {
                            return shapeItem.height / 2 / (instantiator.model.length + 1)
                        }
                        return shapeItem.width / 2 / (instantiator.model.length + 1)
                    }
                    readonly property real shapeRadius: signatureRadius * (idx + 2)

                    active: true
                    model: modelData
                    delegate: page.treemap ? treemapDelegate : radialDelegate
                    onObjectAdded: (index, object) => shapeItem.children.push(object)

                    readonly property Component radialDelegate: Component {
                        SegmentShape {
                            // Qt doc: Note: model, index, and modelData roles are not accessible if the delegate contains required properties, unless it has also required properties with matching names.
                            required property var modelData

                            readonly property bool segmentHover: hoveredSegment === segment.uuid

                            z: (instantiator.model.length - levelInstantiator.idx) // reverse order such that more central levels are above. this gives us segment appearance without having to actually paint segments (instead we stack full cicles)
                            segment: modelData
                            item: shapeItem
                            radius: levelInstantiator.shapeRadius
                            startAngle: -(modelData.start() / 16)
                            sweepAngle: -(modelData.length() / 16)
                            tooltipText: shapeItem.segmentTooltip(segment)
                            showTooltip: !contextMenu && !hoveringListItem && segmentHover && shapeItem.visible
                            fillColor: segmentHover ? Qt.darker(segment.color) : segment.color
                        }
                    }

                    readonly property Component treemapDelegate: Component {
                        TreemapItem {
                            required property var modelData

                            readonly property bool segmentHover: hoveredSegment === segment.uuid

                            z: levelInstantiator.idx + 1 // nested levels are on top of their parents
                            segment: modelData
                            hovered: segmentHover
                        }
                    }
                }
            }

            CenterShape {
                id: centerShape
                z: 500 // on top of everything, arbitrary high number of shapes
                visible: shapeItem.hasShapes && !page.treemap
                segment: RadialMap.rootSegment
                segmentUuid: "root"
                item: shapeItem
                radius: {
                    if (instantiator.model === undefined) {
                        return 0
                    }
                    if (shapeItem.width > shapeItem.height) {
                        return shapeItem.height / 2 / (instantiator.model.length + 1)
                    }
                    return shapeItem.width / 2 / (instantiator.model.length + 1)
                }
                startAngle: 0
                sweepAngle: 360
                tooltipText: shapeItem.rootTooltip()
                showTooltip: !contextMenu && !hoveringListItem && hoveredSegment === segmentUuid && shapeItem.visible
                Kirigami.Theme.colorSet: Kirigami.Theme.View
                fillColor: Kirigami.Theme.backgroundColor
            }

            // The treemap's equivalent of the center circle: the frame around everything with the path at the top.
            TreemapItem {
                id: treemapRoot
                z: 0
                visible: shapeItem.hasShapes && page.treemap && RadialMap.valid
                isRoot: true
                segment: RadialMap.rootSegment
                segmentUuid: "root"
                hovered: hoveredSegment === segmentUuid
                Kirigami.Theme.colorSet: Kirigami.Theme.View
                Kirigami.Theme.inherit: false
            }

            QQC2.Label {
                id: centerLabel
                z: 501
                visible: centerShape.visible
                text: RadialMap.overallSize
                Kirigami.Theme.colorSet: Kirigami.Theme.View
                color: Kirigami.Theme.textColor
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                // The diagonal of the circle is the hypotenuse of the largest square
                readonly property real dimension: (2  * centerShape.radius) / Math.sqrt(2)
                width: dimension
                height: dimension
                anchors.centerIn: parent

                // Let the text scale way down but lock the maximum at the actual default font height.
                // This ensures that the text neatly fits into our dimensions.
                fontSizeMode: Text.Fit
                minimumPixelSize: 2
                Component.onCompleted: font.pixelSize = font.pixelSize
            }

            Kirigami.PlaceholderMessage {
                id: openUrlWarning
                visible: !centerShape.visible && !treemapRoot.visible
                anchors.centerIn: parent
                width: parent.width - (Kirigami.Units.largeSpacing * 4)
                Connections {
                    target: MainContext
                    function onOpenUrlFailed(text, explanation) {
                        openUrlWarning.text = text;
                        openUrlWarning.explanation = explanation;
                    }
                }
            }
        }
    }

    MouseArea {
        x: shapeItem.x
        y: shapeItem.y
        z: 502
        width: shapeItem.width
        height: shapeItem.height
        hoverEnabled: !page.contextMenu
        acceptedButtons: Qt.LeftButton | Qt.RightButton

        function findTarget(mouse) {
            let children = shapeItem.zOrderedShapes
            for (var i in children) {
                const child = children[i]
                if (child === centerLabel) {
                    continue // not part of the shape objects
                }
                if (!child.visible) {
                    continue // e.g. the root shape of the map style not in use
                }
                // Shapes of the radial map span the whole item, rectangles of the treemap do not.
                const contains = child.contains(child.mapFromItem(shapeItem, mouse.x, mouse.y))
                if (contains) {
                    return child
                }
            }
            return null
        }

        onPositionChanged: mouse => {
            mouse.accepted = false
            const child = findTarget(mouse)
            shapeItem.hoveredItem = child
            if (child !== null) {
                hoveringListItem = false
                mouseyX = mouse.x
                mouseyY = mouse.y
                if (child.segmentUuid === undefined) {
                    hoveredSegment = "root"
                } else {
                    hoveredSegment = child.segmentUuid
                }
            } else {
                hoveredSegment = undefined
            }
        }

        onClicked: mouse => {
            const child = findTarget(mouse)
            if (child === null) {
                return
            }
            if (mouse.button === Qt.LeftButton) {
                if (child.segmentUuid === "root") {
                    appWindow.slotUp()
                    return
                }
                if (child.segment.isFilesGroup) {
                    // Cannot open a files group. It describes any number of files.
                    return
                }
                if (!child.segment.isFolder()) {
                    Qt.openUrlExternally(child.url)
                    return
                }
                MainContext.updateURL(child.url)
                MainContext.openUrl(child.url)
            } else if (mouse.button === Qt.RightButton) {
                if (!child.segment.fake) {
                    contextMenuComponent.createObject(child, {segment: child.segment}).popup()
                }
            }
        }
        onPressAndHold: {
            if (mouse.source === Qt.MouseEventNotSynthesized) {
                const child = findTarget(mouse)
                contextMenu.segment = child.segment
                contextMenu.popup()
            }
        }
    }

    DropperItem {
        x: shapeItem.x
        y: shapeItem.y
        z: 503
        width: shapeItem.width
        height: shapeItem.height
        onUrlsDropped: urls => {
            const url = urls[0]
            appWindow.updateURL(url)
            appWindow.openURL(url)
        }
    }

    Kirigami.LoadingPlaceholder {
        id: scanPlaceholder
        visible: page.state === "scanning"
        anchors.centerIn: parent

        Timer {
            interval: 16 // = 60 fps because supposedly Qt hardcodes it all over the place (this claim is very old and it's unclear if still true)
            // Polish doesn't lend itself to the advanced status text https://bugs.kde.org/show_bug.cgi?id=468395
            running: parent.visible && Qt.uiLanguage != 'pl'
            repeat: true
            onTriggered: {
                const files = ScanManager.files();
                const size = ScanManager.totalSize()
                scanPlaceholder.text = i18ncp("Scanned number of files and size so far", "%1 File, %2", "%1 Files, %2", String(files), KCoreAddons.Format.formatByteSize(size));
            }
        }
    }

    Kirigami.PlaceholderMessage {
        visible: page.state === "noData"
        anchors.centerIn: parent
        width: parent.width - (Kirigami.Units.largeSpacing * 4)
        text: i18n("No data available")
    }

    Connections {
        target: ScanManager
        function onAboutToEmptyCache() {
            RadialMap.invalidate()
        }
    }

    Component.onCompleted: {
        appWindow.mapPage = this
    }

    states: [
        State {
            name: "scanning"
            when: ScanManager.running
        },
        State {
            name: "noData"
            // FIXME this toggles too soon causing incomplete shapes to show wtf - but only when instaniatior is no async
            when: !RadialMap.valid && shapeItem.hasShapes
        },
        State {
            name: "" // default state
        }
    ]
}
