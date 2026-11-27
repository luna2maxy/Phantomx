import QtQuick 2.15
import QtQuick.Layouts 1.15
import PhantomXWallet 1.0

Rectangle {
    id: root
    property string title: ""
    property string statusText: ""
    property string statusTone: "accent"
    property string progressText: "—"
    property real progressValue: 0
    property bool progressValid: false
    property bool animating: false
    property bool expanded: false
    property string progressLabel: qsTr("Progress")
    property var metrics: []

    signal toggleExpanded()

    onAnimatingChanged: { if (!root.animating && pulse) pulse.opacity = 1 }

    function toneColor(tone) {
        switch (tone) {
        case "success": return Theme.success
        case "error": return Theme.error
        case "info": return Theme.info
        case "warning": return Theme.warning
        default: return Theme.accent
        }
    }

    implicitWidth: 200
    implicitHeight: headerItem.height + details.height
    radius: Theme.radiusMedium
    border.width: Theme.borderWidth
    border.color: Theme.withAlpha(toneColor(root.statusTone), 0.32)
    color: Theme.surface

    Item {
        id: headerItem
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 42

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.spacing8
            anchors.rightMargin: Theme.spacing8
            spacing: Theme.spacing8

            Rectangle {
                id: pulse
                Layout.preferredWidth: 6
                Layout.preferredHeight: 6
                radius: 3
                color: toneColor(root.statusTone)
                SequentialAnimation on opacity {
                    loops: Animation.Infinite
                    running: root.animating
                    NumberAnimation { to: 0.35; duration: 650 }
                    NumberAnimation { to: 1; duration: 650 }
                }
            }

            Text {
                text: root.title
                color: Theme.text
                font.pixelSize: Theme.fontCaption - 1
                font.weight: Theme.weightBold
                font.letterSpacing: 1
                elide: Text.ElideRight
                Layout.maximumWidth: 58
            }

            Rectangle {
                id: badge
                Layout.preferredHeight: 18
                Layout.preferredWidth: badgeLabel.implicitWidth + 12
                radius: Theme.radiusPill
                color: Theme.withAlpha(toneColor(root.statusTone), 0.16)
                border.width: Theme.borderWidth
                border.color: Theme.withAlpha(toneColor(root.statusTone), 0.45)
                Text {
                    id: badgeLabel
                    anchors.centerIn: parent
                    text: root.statusText
                    color: toneColor(root.statusTone)
                    font.pixelSize: 10
                    font.weight: Theme.weightBold
                    font.capitalization: Font.AllUppercase
                }
            }

            Item { Layout.fillWidth: true }

            Rectangle {
                Layout.preferredWidth: 34
                Layout.preferredHeight: 4
                radius: 2
                color: Theme.withAlpha(Theme.text, Theme.opacitySoft)
                Rectangle {
                    width: root.progressValid ? parent.width * Math.max(0, Math.min(1, root.progressValue)) : 0
                    height: parent.height
                    radius: 2
                    color: toneColor(root.statusTone)
                    Behavior on width { NumberAnimation { duration: Theme.durationNormal; easing.type: Easing.OutCubic } }
                }
            }

            Text {
                text: root.progressText
                color: root.progressValid ? Theme.text : Theme.textMuted
                font.pixelSize: 10
                font.weight: Theme.weightDemi
                Layout.minimumWidth: 30
                horizontalAlignment: Text.AlignRight
            }

            Text {
                text: root.expanded ? "▴" : "▾"
                color: Theme.textMuted
                font.pixelSize: 12
                Layout.preferredWidth: 12
                horizontalAlignment: Text.AlignCenter
            }
        }

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: root.toggleExpanded()
        }
    }

    Item {
        id: details
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: headerItem.bottom
        height: root.expanded ? detailsColumn.implicitHeight : 0
        clip: true
        Behavior on height { NumberAnimation { duration: Theme.durationNormal; easing.type: Easing.OutCubic } }

        ColumnLayout {
            id: detailsColumn
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 6
            spacing: 2

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: Theme.withAlpha(Theme.text, Theme.opacityDivider)
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacing8
                Text {
                    text: root.progressLabel
                    color: Theme.textMuted
                    font.pixelSize: 10
                }
                Item { Layout.fillWidth: true }
                Text {
                    text: root.progressText
                    color: toneColor(root.statusTone)
                    font.pixelSize: 10
                    font.weight: Theme.weightBold
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 6
                radius: 3
                color: Theme.withAlpha(Theme.text, Theme.opacitySoft)
                Rectangle {
                    width: root.progressValid ? parent.width * Math.max(0, Math.min(1, root.progressValue)) : 0
                    height: parent.height
                    radius: 3
                    color: toneColor(root.statusTone)
                    Behavior on width { NumberAnimation { duration: Theme.durationNormal; easing.type: Easing.OutCubic } }
                }
            }

            Repeater {
                model: root.metrics
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0

                    RowLayout {
                        visible: !modelData.fullWidth
                        Layout.fillWidth: true
                        spacing: Theme.spacing8
                        Text {
                            text: modelData.label
                            color: Theme.textMuted
                            font.pixelSize: 10
                            wrapMode: Text.WordWrap
                            elide: Text.ElideNone
                            Layout.preferredWidth: 100
                            Layout.minimumWidth: 84
                            Layout.maximumWidth: 100
                        }
                        Text {
                            text: modelData.value
                            color: modelData.tone ? toneColor(modelData.tone) : Theme.text
                            font.pixelSize: 10
                            font.weight: Theme.weightDemi
                            wrapMode: Text.WordWrap
                            elide: Text.ElideNone
                            horizontalAlignment: Text.AlignRight
                            Layout.fillWidth: true
                            Layout.minimumWidth: 72
                        }
                    }

                    Rectangle {
                        visible: modelData.fullWidth
                        Layout.fillWidth: true
                        Layout.preferredHeight: bannerText.implicitHeight + 4
                        radius: Theme.radiusSmall
                        color: Theme.withAlpha(toneColor(modelData.tone || "error"), 0.10)
                        border.width: Theme.borderWidth
                        border.color: Theme.withAlpha(toneColor(modelData.tone || "error"), 0.28)
                        Text {
                            id: bannerText
                            anchors.fill: parent
                            anchors.margins: 2
                            text: modelData.value
                            color: toneColor(modelData.tone || "error")
                            font.pixelSize: 10
                            font.weight: Theme.weightDemi
                            wrapMode: Text.WordWrap
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }
            }
        }
    }
}
