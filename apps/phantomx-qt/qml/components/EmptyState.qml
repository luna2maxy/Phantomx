import QtQuick 2.15
import QtQuick.Layouts 1.15
import PhantomXWallet 1.0

ColumnLayout {
    id: root
    property string title: qsTr("No data")
    property string message: ""
    property string actionText: ""
    signal actionClicked()

    spacing: Theme.spacing8
    Layout.fillWidth: true

    Text {
        Layout.fillWidth: true
        text: root.title
        color: Theme.textMuted
        font.pixelSize: Theme.fontBody
        font.weight: Theme.weightDemi
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
    }

    Text {
        Layout.fillWidth: true
        visible: root.message.length > 0
        text: root.message
        color: Theme.textMuted
        font.pixelSize: Theme.fontCaption
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
    }

    AppButton {
        Layout.alignment: Qt.AlignHCenter
        visible: root.actionText.length > 0
        text: root.actionText
        variant: "secondary"
        size: "S"
        onClicked: root.actionClicked()
    }
}
