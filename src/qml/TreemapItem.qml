// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
// SPDX-FileCopyrightText: 2026 Septatrix

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.kde.coreaddons as KCoreAddons

import org.kde.filelight

// One rectangle of the treemap.
// The geometry is computed by RadialMap.layoutTreemap().
// Folders whose children are shown become containers with a title bar,
// everything else is drawn as a plain tile.
Rectangle {
    id: tile

    required property Segment segment
    // The folder the map is centered on.
    // It has no color of its own and its title shows the full path.
    property bool isRoot: false
    property bool hovered: false

    // Shown by the single tooltip of the map page, one per tile would be far too many.
    property string tooltipText
    property bool showTooltip: false

    property string segmentUuid: segment ? segment.uuid : ""
    property url url: segment ? segment.url() : ""

    readonly property rect geometry: segment ? segment.treemapRect : Qt.rect(0, 0, 0, 0)
    readonly property bool container: segment ? segment.treemapContainer : false
    readonly property real headerHeight: segment ? segment.treemapHeaderHeight : 0

    readonly property string title: {
        if (!segment) {
            return ""
        }
        if (isRoot) {
            return segment.displayPath()
        }
        // The name of a files group starts with a line break to be centered in the radial map.
        return segment.isFilesGroup ? segment.displayName().trim() : segment.displayName()
    }
    readonly property string size: segment ? segment.humanReadableSize() : ""

    readonly property color schemeColor: isRoot || !segment ? Kirigami.Theme.backgroundColor : segment.color
    // Large areas of fully saturated color are tiring to look at,
    // so tiles are softened towards the background while title bars keep the full color.
    readonly property color baseColor: container ? schemeColor : Kirigami.ColorUtils.tintWithAlpha(Kirigami.Theme.backgroundColor, schemeColor, 0.75)
    readonly property color edgeColor: Qt.darker(baseColor, 1.35)
    readonly property color textColor: Kirigami.ColorUtils.brightnessForColor(container ? headerBar.color : baseColor) === Kirigami.ColorUtils.Dark
        ? Qt.rgba(1, 1, 1, 0.95) : Qt.rgba(0, 0, 0, 0.87)

    x: geometry.x
    y: geometry.y
    width: geometry.width
    height: geometry.height
    visible: width >= 1 && height >= 1
    // Only the title bar of the root is interactive (it leads to the parent folder),
    // gaps between the tiles are not.
    containmentMask: isRoot ? headerBar : null

    // Containers are tinted with the color of their folder so their children stand out.
    color: container ? Kirigami.ColorUtils.tintWithAlpha(Kirigami.Theme.backgroundColor, schemeColor, isRoot ? 0 : 0.25) : baseColor
    opacity: segment && segment.isFilesGroup ? 0.7 : 1
    border.width: hovered ? 2 : (isRoot ? 0 : 1)
    border.color: hovered ? Kirigami.Theme.highlightColor : edgeColor

    // Children too small to be shown individually.
    // Like the files groups in the radial map, but always shown, as there is no other way to tell what the space is.
    Rectangle {
        readonly property rect geometry: tile.segment ? tile.segment.treemapRemainderRect : Qt.rect(0, 0, 0, 0)
        visible: tile.container && width >= 1 && height >= 1
        x: geometry.x
        y: geometry.y
        width: geometry.width
        height: geometry.height
        color: "transparent"
        border.width: 1
        border.color: Qt.rgba(tile.edgeColor.r, tile.edgeColor.g, tile.edgeColor.b, 0.35)
        // The label has two lines, which eliding alone cannot shorten.
        clip: true

        QQC2.Label {
            anchors.fill: parent
            anchors.margins: Kirigami.Units.smallSpacing
            visible: width >= Kirigami.Units.gridUnit * 2 && height >= metrics.height
            text: i18nc("@label area of the treemap made up of files and folders too small to be shown, %1 is their total size",
                        "Smaller items\n%1", KCoreAddons.Format.formatByteSize(tile.segment ? tile.segment.treemapRemainderSize : 0))
            color: Kirigami.Theme.textColor
            opacity: 0.6
            font.italic: true
            elide: Text.ElideRight
            maximumLineCount: Math.max(1, Math.floor(height / metrics.height))
        }
    }

    // Title bar of a container.
    Rectangle {
        id: headerBar
        visible: tile.container
        width: parent.width
        height: tile.headerHeight
        color: tile.isRoot ? Kirigami.Theme.alternateBackgroundColor : tile.baseColor
        border.width: tile.border.width
        border.color: tile.border.color

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Kirigami.Units.smallSpacing
            anchors.rightMargin: Kirigami.Units.smallSpacing
            spacing: Kirigami.Units.smallSpacing

            QQC2.Label {
                Layout.fillWidth: true
                text: tile.title
                color: tile.textColor
                font.weight: Font.DemiBold
                elide: tile.isRoot ? Text.ElideMiddle : Text.ElideRight
                maximumLineCount: 1
            }
            QQC2.Label {
                // Only show the size if there is room for it next to a readable name.
                visible: parent.width > implicitWidth + Kirigami.Units.gridUnit * 4
                text: tile.size
                color: tile.textColor
                opacity: 0.8
                maximumLineCount: 1
            }
        }
    }

    FontMetrics {
        id: metrics
    }

    // Name and size of a tile.
    ColumnLayout {
        id: tileLabels
        visible: !tile.container && width >= Kirigami.Units.gridUnit * 2 && height >= metrics.height
        anchors.fill: parent
        anchors.margins: Kirigami.Units.smallSpacing
        spacing: 0

        QQC2.Label {
            id: nameLabel
            Layout.fillWidth: true
            text: tile.title
            color: tile.textColor
            font.italic: tile.segment ? tile.segment.isFilesGroup : false
            // Eliding in the middle keeps the file extension visible,
            // the start of a folder name is more telling than its end.
            elide: tile.segment && !tile.segment.isFolder() ? Text.ElideMiddle : Text.ElideRight
            maximumLineCount: 1
        }
        QQC2.Label {
            id: sizeLabel
            Layout.fillWidth: true
            visible: tileLabels.height >= 2 * metrics.height
            text: tile.size
            color: tile.textColor
            opacity: 0.8
            font: Kirigami.Theme.smallFont
            elide: Text.ElideRight
            maximumLineCount: 1
        }
        Item {
            Layout.fillHeight: true
        }
    }
}
