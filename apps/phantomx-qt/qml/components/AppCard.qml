import QtQuick 2.15
import QtQuick.Layouts 1.15
import PhantomXWallet 1.0

Rectangle {
    id: card
    default property alias content: contentCol.data
    property bool compact: false
    property int padding: compact ? Theme.cardPaddingCompact : Theme.cardPadding

    color: Theme.surface
    radius: Theme.cardRadius
    border.width: Theme.borderWidth
    border.color: Theme.border
    implicitHeight: contentCol.implicitHeight + padding * 2
    Layout.fillWidth: true

    ColumnLayout {
        id: contentCol
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: card.padding
        spacing: Theme.cardGap
    }
}
