import QtQuick 2.15
import QtQuick.Layouts 1.15
import PhantomXWallet 1.0

Item {
    id: root
    default property alias content: row.data
    property bool showDivider: true
    property bool hovered: mouse.containsMouse
    property bool clickable: false
    signal clicked()

    implicitHeight: Math.max(Theme.rowHeight, row.implicitHeight + Theme.spacing12 * 2)
    Layout.fillWidth: true

    Rectangle {
        anchors.fill: parent
        anchors.margins: 2
        radius: Theme.radiusSmall
        color: root.hovered ? Theme.rowHoverColor : "transparent"
    }

    RowLayout {
        id: row
        anchors.fill: parent
        anchors.leftMargin: Theme.rowPaddingX
        anchors.rightMargin: Theme.rowPaddingX
        spacing: Theme.spacing12
    }

    Rectangle {
        visible: root.showDivider
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: Theme.rowPaddingX
        anchors.rightMargin: Theme.rowPaddingX
        height: Theme.borderWidth
        color: Theme.dividerColor
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: root.clickable ? Qt.LeftButton : Qt.NoButton
        onClicked: if (root.clickable) root.clicked()
        cursorShape: root.clickable ? Qt.PointingHandCursor : Qt.ArrowCursor
    }
}
