import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import PhantomXWallet 1.0

Rectangle {
    id: root
    property alias text: input.text
    property string placeholderText: qsTr("YYYY-MM-DD")

    implicitHeight: Theme.controlHeight
    radius: Theme.radiusMedium
    color: Theme.surfaceRaised
    border.width: Theme.borderWidth
    border.color: input.activeFocus ? Theme.accent : (input.hovered ? Theme.borderStrong : Theme.border)

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.spacing12
        anchors.rightMargin: Theme.spacing12
        spacing: Theme.spacing8

        TextField {
            id: input
            Layout.fillWidth: true
            Layout.fillHeight: true
            background: null
            placeholderText: root.placeholderText
            color: Theme.text
            placeholderTextColor: Theme.textMuted
            selectionColor: Theme.accent
            selectedTextColor: Theme.textInverse
            font.pixelSize: Theme.fontBody
            verticalAlignment: TextInput.AlignVCenter
        }

        Text {
            text: "▦"
            color: Theme.textMuted
            font.pixelSize: Theme.fontBody
            verticalAlignment: Text.AlignVCenter
        }
    }
}
