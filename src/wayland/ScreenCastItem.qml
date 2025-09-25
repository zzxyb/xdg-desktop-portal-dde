// SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

Rectangle {
    id: rect

    color: "#99FFFFFF"
    radius: 6

    property string iconPath

    Component {
        id: icon

        Rectangle {
            color: "red"
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        RowLayout {
            Loader {
                sourceComponent: rect.iconPath ? icon : undefined
            }

            Label {
                text: "11111111111"
                elide: Text.ElideRight
                font.pixelSize: 14
            }
        }

        PreViewItem {
            Layout.fillHeight: true
            Layout.fillWidth: true
        }
    }
}
