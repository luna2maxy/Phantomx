import QtQuick 2.15
import QtQuick.Layouts 1.15
import PhantomXWallet 1.0

Rectangle {
    id: root
    property string pageTitle: ""
    property bool online: false
    property bool usingLocalData: false
    color: Theme.bg
    border.width: 0

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: Theme.borderWidth
        color: Theme.border
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.pageMargin
        anchors.rightMargin: Theme.pageMargin
        spacing: Theme.spacing12

        Text {
            Layout.fillWidth: true
            text: root.pageTitle
            color: Theme.text
            font.pixelSize: Theme.fontTitle
            font.weight: Theme.weightBold
            elide: Text.ElideRight
        }

        Rectangle {
            width: Theme.statusDotSize; height: Theme.statusDotSize; radius: Theme.radiusPill
            color: root.online ? Theme.success : (root.usingLocalData ? Theme.warning : Theme.error)
        }
        Text {
            text: root.online ? qsTr("Node Online") : (root.usingLocalData ? qsTr("Offline (Local Data)") : qsTr("Node Offline"))
            color: root.online ? Theme.success : (root.usingLocalData ? Theme.warning : Theme.error)
            font.pixelSize: Theme.fontBody
            font.weight: Theme.weightDemi
        }
    }
}
