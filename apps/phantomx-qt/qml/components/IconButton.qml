import QtQuick 2.15
import QtQuick.Controls 2.15
import PhantomXWallet 1.0

Rectangle {
    id: root
    property string iconText: ""
    property string tooltipText: ""
    property bool enabled: true
    signal clicked()

    implicitWidth: 28
    implicitHeight: 28
    radius: Theme.radiusSmall
    color: !root.enabled ? "transparent" : (mouse.pressed ? Theme.withAlpha(Theme.text, Theme.opacityPressed) : (mouse.containsMouse ? Theme.withAlpha(Theme.text, Theme.opacityHover) : "transparent"))
    border.width: 0
    opacity: root.enabled ? 1.0 : Theme.opacityDisabled

    Accessible.name: root.tooltipText.length > 0 ? root.tooltipText : root.iconText
    Accessible.role: Accessible.Button

    Text {
        anchors.centerIn: parent
        text: root.iconText
        color: Theme.textMuted
        font.pixelSize: Theme.fontBody
        verticalAlignment: Text.AlignVCenter
        horizontalAlignment: Text.AlignHCenter
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        enabled: root.enabled
        cursorShape: root.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
        onClicked: root.clicked()
    }

    AppTooltip {
        text: root.tooltipText
        visible: mouse.containsMouse && root.tooltipText.length > 0
    }
}
