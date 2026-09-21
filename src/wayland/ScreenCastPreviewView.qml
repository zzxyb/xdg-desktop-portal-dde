// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

import QtQuick

import screencast 1.0

Item {
    id: root

    property int sourceType: PortalCommon.Monitor
    property var outputsModel
    property int outputIndex: -1
    property var toplevelsModel
    property int toplevelIndex: -1
    property bool showCursor: true
    property bool active: true

    Loader {
        anchors.fill: parent
        active: root.active
        sourceComponent: previewComponent
    }

    Component {
        id: previewComponent

        ScreenCastPreviewItem {
            sourceType: root.sourceType
            outputsModel: root.outputsModel
            outputIndex: root.outputIndex
            toplevelsModel: root.toplevelsModel
            toplevelIndex: root.toplevelIndex
            showCursor: root.showCursor
        }
    }
}
