import QtQuick 2.15
import QtQuick.Layouts 1.15
import PhantomXWallet 1.0

Rectangle {
    id: root
    default property alias content: col.data
    property int padding: 0

    color: Theme.surface
    radius: Theme.cardRadius
    border.width: Theme.borderWidth
    border.color: Theme.border
    implicitHeight: col.implicitHeight + padding * 2
    Layout.fillWidth: true

    // No clip to avoid rounded-corner clipping issues; rows use inset hover.
    ColumnLayout {
        id: col
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: root.padding
        spacing: 0
    }
}
