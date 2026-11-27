import QtQuick 2.15
import QtQuick.Layouts 1.15
import PhantomXWallet 1.0

Rectangle {
    id: root
    property string syncText: "—"
    property string note: ""
    color: Theme.overlay
    border.width: 0

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: Theme.borderWidth
        color: Theme.border
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.pageMargin
        anchors.rightMargin: Theme.pageMargin
        spacing: Theme.spacing12

        Text {
            text: root.syncText
            color: Theme.textMuted
            font.pixelSize: Theme.fontCaption
            elide: Text.ElideRight
        }
        Text {
            Layout.fillWidth: true
            text: root.note
            color: Theme.textMuted
            font.pixelSize: Theme.fontCaption
            elide: Text.ElideRight
        }
    }
}
