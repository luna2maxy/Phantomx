import QtQuick 2.15
import QtQuick.Layouts 1.15
import PhantomXWallet 1.0

Rectangle {
    id: root
    property var items: []
    property int currentIndex: 0
    signal selected(int index)

    implicitHeight: Theme.controlHeight
    implicitWidth: row.implicitWidth + Theme.spacing8
    radius: Theme.radiusMedium
    color: Theme.surfaceRaised
    border.width: Theme.borderWidth
    border.color: Theme.border

    RowLayout {
        id: row
        anchors.fill: parent
        anchors.margins: Theme.spacing4
        spacing: Theme.spacing4

        Repeater {
            model: root.items
            delegate: Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: Theme.radiusSmall
                color: root.currentIndex === index ? Theme.accent : (segMouse.containsMouse ? Theme.withAlpha(Theme.text, Theme.opacityHover) : "transparent")

                Text {
                    anchors.centerIn: parent
                    text: modelData
                    color: root.currentIndex === index ? Theme.textInverse : Theme.text
                    font.pixelSize: Theme.fontBody
                    font.weight: Theme.weightDemi
                    elide: Text.ElideRight
                }

                MouseArea {
                    id: segMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: { root.currentIndex = index; root.selected(index) }
                }
            }
        }
    }
}
