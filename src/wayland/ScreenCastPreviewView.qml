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
    readonly property bool useSoftwareRenderer: GraphicsInfo.api === GraphicsInfo.Software

    Loader {
        id: previewLoader

        anchors.fill: parent
        active: root.active
        sourceComponent: root.useSoftwareRenderer ? paintedPreview : rhiPreview
    }

    Component {
        id: rhiPreview

        ScreenCastPreviewItem {
            sourceType: root.sourceType
            outputsModel: root.outputsModel
            outputIndex: root.outputIndex
            toplevelsModel: root.toplevelsModel
            toplevelIndex: root.toplevelIndex
            showCursor: root.showCursor
        }
    }

    Component {
        id: paintedPreview

        ScreenCastPreviewPaintedItem {
            sourceType: root.sourceType
            outputsModel: root.outputsModel
            outputIndex: root.outputIndex
            toplevelsModel: root.toplevelsModel
            toplevelIndex: root.toplevelIndex
            showCursor: root.showCursor
        }
    }
}
