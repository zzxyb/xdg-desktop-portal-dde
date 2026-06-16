// SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

import QtQuick
import QtQuick.Controls

GridView {
    id: view

    property real cardWidth: 236
    property real cardHeight: 166
    property bool previewActive: true

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
                height: parent.height - screenLabel.height - 20
                active: view.previewActive
                sourceType: PortalCommon.Monitor
                outputsModel: view.model
                outputIndex: index
                showCursor: true
            }

            Label {
                id: screenLabel

                anchors {
                    bottom: parent.bottom
                    left: parent.left
                    right: parent.right
                    margins: 6
                }
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
                text: screenName
            }

            MouseArea {
                anchors.fill: parent
                onClicked: view.currentIndex = index
            }
        }
    }

    ScrollBar.vertical: ScrollBar {}
}
