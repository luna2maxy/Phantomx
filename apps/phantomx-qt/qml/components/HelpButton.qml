import QtQuick 2.15
import QtQuick.Controls 2.15
import PhantomXWallet 1.0

Rectangle {
    id: root
    property string tooltipText: ""
    signal clicked()

    implicitWidth: 24
    implicitHeight: 24
    radius: Theme.radiusPill
    color: mouse.containsMouse ? Theme.withAlpha(Theme.text, Theme.opacityHover) : "transparent"
    border.width: Theme.borderWidth
    border.color: Theme.border

    Accessible.name: qsTr("Help")
    Accessible.role: Accessible.Button

    Text {
        anchors.centerIn: parent
        text: "?"
        color: Theme.textMuted
        font.pixelSize: Theme.fontCaption
        font.weight: Theme.weightBold
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }

    AppTooltip {
        text: root.tooltipText
        visible: mouse.containsMouse && root.tooltipText.length > 0
    }
}
