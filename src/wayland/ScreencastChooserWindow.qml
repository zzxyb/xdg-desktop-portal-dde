// SPDX-FileCopyrightText: 2025-2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: LGPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

import screencast 1.0

import org.deepin.dtk 1.0 as D

D.DialogWindow {
    id: root

    width: 840
    height: 584
    minimumWidth: width
    minimumHeight: height
    maximumWidth: width
    maximumHeight: height
    modality: Qt.WindowModal

    property alias allowRestore: restoreCheckBox.checked
    property int viewLayoutIndex: 0
    property int outputIndex: -1
    property int toplevelIndex: -1
    property alias outputsModel: screensModel
    property alias toplevelsModel: toplevelsModelObject
    property string clientAppName
    property int allowedSourceTypes: 1
    readonly property bool graphicsApiReady: graphicsInfoProbe.api !== GraphicsInfo.Unknown
    readonly property bool useSoftwareRenderer: graphicsInfoProbe.api === GraphicsInfo.Software
    readonly property real itemMargin: 10
    readonly property real scrollBarMargin: 50

    signal accept()
    signal reject()

    Item {
        id: graphicsInfoProbe

        readonly property int api: GraphicsInfo.api
        visible: false
    }

    ScreenListModel {
        id: screensModel
    }

    ToplevelListModel {
        id: toplevelsModelObject
    }

    ColumnLayout {
        spacing: 8
        width: parent.width

        Label {
            text: qsTr("Application [%1] requests to share you screen content").arg(clientAppName)
            font.bold: true
            Layout.alignment: Qt.AlignTop | Qt.AlignHCenter
        }

        Label {
            text: qsTr("Please select the screen or window you wish to share")
            Layout.alignment: Qt.AlignTop | Qt.AlignHCenter
        }

        RowLayout {
            spacing: root.itemMargin
            Layout.alignment: Qt.AlignHCenter
            Button {
                text: qsTr("Screen")
                visible: (root.allowedSourceTypes & 1) !== 0
                highlighted: root.viewLayoutIndex === 0
                flat: !highlighted
                onClicked: root.viewLayoutIndex = 0
            }
            Button {
                text: qsTr("Window")
                visible: (root.allowedSourceTypes & 2) !== 0
                highlighted: root.viewLayoutIndex === 1
                flat: !highlighted
                onClicked: root.viewLayoutIndex = 1
            }
        }

        StackLayout {
            id: viewLayout

            readonly property real viewHeight: 372
            readonly property real radius: 6
            readonly property real delegateHeight: 36
            readonly property color darkColor: "black"
            readonly property color lightColor: "white"

            Layout.preferredWidth: parent.width
            Layout.preferredHeight: viewHeight
            currentIndex: root.viewLayoutIndex

            Loader {
                Layout.fillWidth: true
                Layout.fillHeight: true
                sourceComponent: !root.graphicsApiReady ? null
                                                        : root.useSoftwareRenderer ? legacyScreensComponent
                                                                                   : previewScreensComponent
            }

            Loader {
                Layout.fillWidth: true
                Layout.fillHeight: true
                sourceComponent: !root.graphicsApiReady ? null
                                                        : root.useSoftwareRenderer ? legacyToplevelsComponent
                                                                                   : previewToplevelsComponent
            }
        }

        RowLayout {
            spacing: root.itemMargin
            Item {
                Layout.fillWidth: true

                CheckBox {
                    id: restoreCheckBox

                    anchors.verticalCenter: parent.verticalCenter
                    checked: true
                    text: qsTr("Allow restoring on future sessions")
                }
            }

            Row {
                Layout.alignment: Qt.AlignHCenter
                spacing: root.itemMargin
                Button {
                    id: acceptBtn

                    text: qsTr("Accept")
                    enabled: (root.viewLayoutIndex === 0 && root.outputIndex >= 0) ||
                             (root.viewLayoutIndex === 1 && root.toplevelIndex >= 0)
                    onClicked: root.accept()
                }
                D.RecommandButton {
                    text: qsTr("Reject")
                    onClicked: root.reject()
                }
            }

            Item {
                Layout.fillWidth: true
            }
        }
    }

    Component {
        id: legacyScreensComponent

        Background {
            radius: viewLayout.radius
            darkColor: viewLayout.darkColor
            lightColor: viewLayout.lightColor

            OutputListView {
                anchors.fill: parent
                rightMargin: root.scrollBarMargin
                model: screensModel
                itemHeight: viewLayout.delegateHeight
                currentIndex: root.outputIndex
                onCurrentIndexChanged: root.outputIndex = currentIndex
            }
        }
    }

    Component {
        id: legacyToplevelsComponent

        Background {
            radius: viewLayout.radius
            darkColor: viewLayout.darkColor
            lightColor: viewLayout.lightColor

            ToplevelList {
                anchors.fill: parent
                rightMargin: root.scrollBarMargin
                model: toplevelsModelObject
                itemHeight: viewLayout.delegateHeight
                currentIndex: root.toplevelIndex
                onCurrentIndexChanged: root.toplevelIndex = currentIndex
            }
        }
    }

    Component {
        id: previewScreensComponent

        Background {
            radius: viewLayout.radius
            darkColor: viewLayout.darkColor
            lightColor: viewLayout.lightColor

            OutputPreviewGrid {
                anchors.fill: parent
                model: screensModel
                currentIndex: root.outputIndex
                previewActive: root.viewLayoutIndex === 0
                onCurrentIndexChanged: root.outputIndex = currentIndex
            }
        }
    }

    Component {
        id: previewToplevelsComponent

        Background {
            radius: viewLayout.radius
            darkColor: viewLayout.darkColor
            lightColor: viewLayout.lightColor

            ToplevelPreviewGrid {
                anchors.fill: parent
                model: toplevelsModelObject
                currentIndex: root.toplevelIndex
                previewActive: root.viewLayoutIndex === 1
                onCurrentIndexChanged: root.toplevelIndex = currentIndex
            }
        }
    }
}
