// SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import org.deepin.dtk 1.0 as D

GridView {
    id: view

    property real cardWidth: 236
    property real cardHeight: 180
    property bool previewActive: true
    readonly property real iconSize: 16

    clip: true
    cellWidth: Math.max(cardWidth, width / Math.max(1, Math.floor(width / cardWidth)))
    cellHeight: cardHeight
    boundsBehavior: Flickable.StopAtBounds

    delegate: Item {
        width: view.cellWidth
        height: view.cellHeight

        Rectangle {
            anchors.fill: parent
            anchors.margins: 6
            radius: 8
            color: "transparent"
            border.width: GridView.isCurrentItem ? 2 : 1
            border.color: GridView.isCurrentItem ? palette.highlight : palette.mid

            ScreenCastPreviewView {
                anchors {
                    top: parent.top
                    left: parent.left
                    right: parent.right
                    margins: 6
                }
                height: parent.height - windowInfo.height - 20
                active: view.previewActive
                sourceType: PortalCommon.Window
                toplevelsModel: view.model
                toplevelIndex: index
                showCursor: true
            }

            RowLayout {
                id: windowInfo

                anchors {
                    bottom: parent.bottom
                    left: parent.left
                    right: parent.right
                    margins: 6
                }
                spacing: 6

                D.DciIcon {
                    Layout.preferredWidth: view.iconSize
                    Layout.preferredHeight: view.iconSize
                    name: appIcon
                    sourceSize: Qt.size(width, height)
                }
                Label {
                    text: title.length > 0 ? title : name
                    opacity: 0.85
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
            }

            MouseArea {
                anchors.fill: parent
                onClicked: view.currentIndex = index
            }
        }
    }

    ScrollBar.vertical: ScrollBar {}
}
