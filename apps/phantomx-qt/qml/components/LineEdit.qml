import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import PhantomXWallet 1.0

ColumnLayout {
    id: root
    property alias text: input.text
    property alias placeholderText: input.placeholderText
    property alias echoMode: input.echoMode
    property alias inputMethodHints: input.inputMethodHints
    property alias validator: input.validator
    property bool error: false
    property string errorText: ""
    property string leftIconText: ""
    property string rightIconText: ""
    property string chipText: ""
    signal chipClicked()
    signal rightIconClicked()
    signal accepted()

    spacing: Theme.spacing4
    Layout.fillWidth: true

    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: Theme.controlHeight
        radius: Theme.radiusMedium
        color: Theme.surfaceRaised
        border.width: Theme.borderWidth
        border.color: root.error ? Theme.error : (input.activeFocus ? Theme.accent : (input.hovered ? Theme.borderStrong : Theme.border))

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.spacing12
            anchors.rightMargin: Theme.spacing8
            spacing: Theme.spacing8

            Text {
                visible: root.leftIconText.length > 0
                text: root.leftIconText
                color: Theme.textMuted
                font.pixelSize: Theme.fontBody
                verticalAlignment: Text.AlignVCenter
            }

            TextField {
                id: input
                Layout.fillWidth: true
                Layout.fillHeight: true
                background: null
                color: Theme.text
                placeholderTextColor: Theme.textMuted
                selectionColor: Theme.accent
                selectedTextColor: Theme.textInverse
                font.pixelSize: Theme.fontBody
                verticalAlignment: TextInput.AlignVCenter
                onAccepted: root.accepted()
            }

            Rectangle {
                visible: root.chipText.length > 0
                radius: Theme.radiusSmall
                color: chipMouse.containsMouse ? Theme.withAlpha(Theme.accent, Theme.opacitySoft) : Theme.withAlpha(Theme.accent, 0.18)
                border.width: Theme.borderWidth
                border.color: Theme.withAlpha(Theme.accent, Theme.opacityBorderSoft)
                implicitHeight: 24
                implicitWidth: chipLabel.implicitWidth + Theme.spacing12
                Text {
                    id: chipLabel
                    anchors.centerIn: parent
                    text: root.chipText
                    color: Theme.accent
                    font.pixelSize: Theme.fontCaption
                    font.weight: Theme.weightDemi
                }
                MouseArea { id: chipMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.chipClicked() }
            }

            Text {
                visible: root.rightIconText.length > 0
                text: root.rightIconText
                color: Theme.textMuted
                font.pixelSize: Theme.fontBody
                verticalAlignment: Text.AlignVCenter
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.rightIconClicked() }
            }
        }
    }

    Text {
        Layout.fillWidth: true
        visible: root.error && root.errorText.length > 0
        text: root.errorText
        color: Theme.error
        font.pixelSize: Theme.fontCaption
        wrapMode: Text.WordWrap
    }
}
