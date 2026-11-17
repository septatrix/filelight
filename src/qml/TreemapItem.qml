// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
// SPDX-FileCopyrightText: 2026 Septatrix

import QtQuick
import org.kde.kirigami as Kirigami
import org.kde.coreaddons as KCoreAddons

import org.kde.filelight

// One rectangle of the treemap.
// The geometry is computed by RadialMap.layoutTreemap().
// Folders whose children are shown become containers with a title bar,
// everything else is drawn as a plain tile.
//
// There can be thousands of tiles, so keep them cheap:
// a tile is a single rectangle, and its labels are only created when they fit.
// Labels are plain Text rather than QQC2.Label for the same reason,
// so they have to pick up the theme's fonts themselves.
Rectangle {
    id: tile

    required property Segment segment
    // The folder the map is centered on.
    // It has no color of its own and its title shows the full path.
    property bool isRoot: false
    property bool hovered: false

    property string segmentUuid: segment ? segment.uuid : ""
    property url url: segment ? segment.url() : ""

    readonly property rect geometry: segment ? segment.treemapRect : Qt.rect(0, 0, 0, 0)
    readonly property bool container: segment ? segment.treemapContainer : false
    readonly property real headerHeight: segment ? segment.treemapHeaderHeight : 0

    readonly property color schemeColor: isRoot || !segment ? Kirigami.Theme.backgroundColor : segment.color
    // Large areas of fully saturated color are tiring to look at,
    // so tiles are softened towards the background while title bars keep the full color.
    readonly property color baseColor: container ? schemeColor : Kirigami.ColorUtils.tintWithAlpha(Kirigami.Theme.backgroundColor, schemeColor, 0.75)
    readonly property color edgeColor: Qt.darker(baseColor, 1.35)

    x: geometry.x
    y: geometry.y
    width: geometry.width
    height: geometry.height
    // Tiles that do not fit are collapsed to nothing and not drawn at all.
    visible: width >= 1 && height >= 1
    // Only the title bar of the root is interactive (it leads to the parent folder),
    // gaps between the tiles are not.
    containmentMask: isRoot ? decoration : null

    // Containers are tinted with the color of their folder so their children stand out.
    color: container ? Kirigami.ColorUtils.tintWithAlpha(Kirigami.Theme.backgroundColor, schemeColor, isRoot ? 0 : 0.25) : baseColor
    opacity: segment && segment.isFilesGroup ? 0.7 : 1
    border.width: hovered ? 2 : (isRoot ? 0 : 1)
    border.color: hovered ? Kirigami.Theme.highlightColor : edgeColor

    // The title bar of a container, or the labels of a tile.
    // For a container this item covers the title bar.
    Loader {
        id: decoration
        width: parent.width
        height: tile.container ? tile.headerHeight : parent.height
        active: tile.visible && (tile.container || (tile.width >= Kirigami.Units.gridUnit * 2 && tile.height >= Kirigami.Units.gridUnit))
        sourceComponent: tile.container ? containerComponent : labelsComponent
    }

    Component {
        id: containerComponent

        Item {
            readonly property color headerColor: tile.isRoot ? Kirigami.Theme.alternateBackgroundColor : tile.baseColor
            readonly property color textColor: Kirigami.ColorUtils.brightnessForColor(headerColor) === Kirigami.ColorUtils.Dark
                ? Qt.rgba(1, 1, 1, 0.95) : Qt.rgba(0, 0, 0, 0.87)

            // Children too small to be shown individually.
            // Like the files groups in the radial map, but always shown, as there is no other way to tell what the space is.
            Rectangle {
                readonly property rect geometry: tile.segment.treemapRemainderRect
                visible: width >= 1 && height >= 1
                x: geometry.x
                y: geometry.y
                width: geometry.width
                height: geometry.height
                color: "transparent"
                border.width: 1
                border.color: Qt.rgba(tile.edgeColor.r, tile.edgeColor.g, tile.edgeColor.b, 0.35)
                // The label has two lines, which eliding alone cannot shorten.
                clip: true

                Text {
                    font.family: Kirigami.Theme.defaultFont.family
                    font.pointSize: Kirigami.Theme.defaultFont.pointSize
                    anchors.fill: parent
                    anchors.margins: Kirigami.Units.smallSpacing
                    visible: width >= Kirigami.Units.gridUnit * 2 && height >= Kirigami.Units.gridUnit
                    text: i18nc("@label area of the treemap made up of files and folders too small to be shown, %1 is their total size",
                                "Smaller items\n%1", KCoreAddons.Format.formatByteSize(tile.segment.treemapRemainderSize))
                    color: Kirigami.Theme.textColor
                    opacity: 0.6
                    font.italic: true
                    elide: Text.ElideRight
                }
            }

            Rectangle {
                width: parent.width
                height: parent.height
                color: parent.headerColor
                border.width: tile.border.width
                border.color: tile.border.color

                Text {
                    font.family: Kirigami.Theme.defaultFont.family
                    font.pointSize: Kirigami.Theme.defaultFont.pointSize
                    id: sizeText
                    anchors.right: parent.right
                    anchors.rightMargin: Kirigami.Units.smallSpacing
                    anchors.verticalCenter: parent.verticalCenter
                    // Only show the size if there is room for it next to a readable name.
                    visible: parent.width > implicitWidth + Kirigami.Units.gridUnit * 4
                    text: tile.segment.humanReadableSize()
                    color: parent.parent.textColor
                    opacity: 0.8
                }
                Text {
                    font.family: Kirigami.Theme.defaultFont.family
                    font.pointSize: Kirigami.Theme.defaultFont.pointSize
                    anchors.left: parent.left
                    anchors.right: sizeText.visible ? sizeText.left : parent.right
                    anchors.leftMargin: Kirigami.Units.smallSpacing
                    anchors.rightMargin: Kirigami.Units.smallSpacing
                    anchors.verticalCenter: parent.verticalCenter
                    text: tile.isRoot ? tile.segment.displayPath() : tile.segment.displayName()
                    color: parent.parent.textColor
                    font.weight: Font.DemiBold
                    elide: tile.isRoot ? Text.ElideMiddle : Text.ElideRight
                }
            }
        }
    }

    Component {
        id: labelsComponent

        Column {
            readonly property color textColor: Kirigami.ColorUtils.brightnessForColor(tile.baseColor) === Kirigami.ColorUtils.Dark
                ? Qt.rgba(1, 1, 1, 0.95) : Qt.rgba(0, 0, 0, 0.87)

            padding: Kirigami.Units.smallSpacing
            clip: true

            Text {
                font.family: Kirigami.Theme.defaultFont.family
                font.pointSize: Kirigami.Theme.defaultFont.pointSize
                width: parent.width - 2 * parent.padding
                // The name of a files group starts with a line break to be centered in the radial map.
                text: tile.segment.isFilesGroup ? tile.segment.displayName().trim() : tile.segment.displayName()
                color: parent.textColor
                font.italic: tile.segment.isFilesGroup
                // Eliding in the middle keeps the file extension visible,
                // the start of a folder name is more telling than its end.
                elide: tile.segment.isFolder() ? Text.ElideRight : Text.ElideMiddle
            }
            Text {
                width: parent.width - 2 * parent.padding
                visible: tile.height >= 2 * Kirigami.Units.gridUnit
                text: tile.segment.humanReadableSize()
                color: parent.textColor
                opacity: 0.8
                font.family: Kirigami.Theme.smallFont.family
                font.pointSize: Kirigami.Theme.smallFont.pointSize
                elide: Text.ElideRight
            }
        }
    }
}
