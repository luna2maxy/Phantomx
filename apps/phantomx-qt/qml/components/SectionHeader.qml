import QtQuick 2.15
import QtQuick.Layouts 1.15
import PhantomXWallet 1.0

ColumnLayout {
    id: root
    property string text: ""
    property bool showLine: true
    spacing: Theme.spacing8
    Layout.fillWidth: true

    Text {
        Layout.fillWidth: true
        text: root.text
        color: Theme.text
        font.pixelSize: Theme.fontBody
        font.weight: Theme.weightDemi
        elide: Text.ElideRight
    }

    Rectangle {
        visible: root.showLine
        Layout.preferredWidth: 40
        Layout.preferredHeight: Theme.borderWidth
        color: Theme.accent
    }
}
