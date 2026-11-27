import QtQuick 2.15
import QtQuick.Layouts 1.15
import PhantomXWallet 1.0

Item {
    id: row
    property string label: ""
    property string value: "—"
    property string badge: ""
    property string badgeTone: "muted"
    property bool dimmed: false

    implicitHeight: Theme.statusRowHeight
    Layout.fillWidth: true
    Layout.preferredHeight: Theme.statusRowHeight

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: Theme.borderWidth
        color: Theme.border
        opacity: Theme.opacityDivider
    }

    RowLayout {
        anchors.fill: parent
        spacing: Theme.spacing8

        Text {
            Layout.preferredWidth: Math.max(120, row.width * 0.42)
            Layout.fillHeight: true
            text: row.label
            color: Theme.textMuted
            font.pixelSize: Theme.fontCaption
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
            opacity: row.dimmed ? Theme.opacityMuted : 1.0
        }

        Text {
            Layout.fillWidth: true
            Layout.fillHeight: true
            text: row.value.length > 0 ? row.value : "—"
            color: (row.value === "—" || row.value.length === 0) ? Theme.textMuted : Theme.text
            font.pixelSize: Theme.fontBody
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
            opacity: row.dimmed ? Theme.opacityMuted : 1.0
        }

        AppBadge {
            visible: row.badge.length > 0
            text: row.badge
            tone: row.badgeTone
            opacity: row.dimmed ? Theme.opacityMuted : 1.0
        }
    }
}
