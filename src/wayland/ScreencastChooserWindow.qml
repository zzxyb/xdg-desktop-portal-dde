// SPDX-FileCopyrightText: 2025 UnionTech Software Technology Co., Ltd.
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
    property alias viewLayoutIndex: viewLayout.currentIndex
    property alias outputIndex: screensView.currentIndex
    property alias toplevelIndex: toplevelsView.currentIndex
    property var outputsModel: screensView.model
    property var toplevelsModel: toplevelsView.model
    property string clientAppName
    readonly property real itemMargin: 10
    readonly property bool isScreenView: viewLayout.currentIndex === 0

    signal accept()
    signal reject()

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
                highlighted: viewLayout.currentIndex === 0
                flat: !highlighted
                onClicked: viewLayout.currentIndex = 0
            }
            Button {
                text: qsTr("Window")
                highlighted: viewLayout.currentIndex === 1
                flat: !highlighted
                onClicked: viewLayout.currentIndex = 1
            }
        }

        ColumnLayout {
            spacing: root.itemMargin
            Layout.preferredWidth: parent.width
            Layout.preferredHeight: 372

            StackLayout {
                id: viewLayout

                readonly property real radius: 6
                readonly property color darkColor: "black"
                readonly property color lightColor: "white"

                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: 0
                Background {
                    radius: parent.radius
                    darkColor: parent.darkColor
                    lightColor: parent.lightColor
                    OutputListView {
                        id: screensView

                        anchors.fill: parent
                        model: ScreenListModel {}
                        currentIndex: -1
                        previewActive: root.isScreenView
                    }
                }

                Background {
                    radius: parent.radius
                    darkColor: parent.darkColor
                    lightColor: parent.lightColor
                    ToplevelList {
                        id: toplevelsView

                        anchors.fill: parent
                        model: ToplevelListModel {}
                        currentIndex: -1
                        previewActive: !root.isScreenView
                    }
                }
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
                    onClicked: root.accept();
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
}
