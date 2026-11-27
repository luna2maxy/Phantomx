import QtQuick 2.15
import QtQuick.Controls 2.15
import PhantomXWallet 1.0

Button {
    id: control
    property bool __canClick: control.enabled && !control.loading
    property string variant: "secondary" // primary, secondary, ghost, danger
    property string size: "M" // S, M, L
    property bool loading: false

    implicitHeight: size === "S" ? Theme.buttonHeightS : (size === "L" ? Theme.buttonHeightL : Theme.buttonHeightM)
    implicitWidth: Math.max(implicitHeight * 2, contentRow.implicitWidth + Theme.spacing16 * 2)
    leftPadding: Theme.spacing16
    rightPadding: Theme.spacing16
    hoverEnabled: !control.loading
    opacity: (control.enabled && !control.loading) ? 1.0 : Theme.opacityDisabled
    Accessible.name: text

    background: Rectangle {
        radius: Theme.radiusMedium
        border.width: Theme.borderWidth
        border.color: {
            if (!control.enabled) return Theme.border
            if (control.variant === "primary") return Theme.accent
            if (control.variant === "danger") return Theme.error
            return Theme.border
        }
        color: {
            if (control.variant === "primary") return control.enabled ? Theme.accent : Theme.surfaceRaised
            if (control.variant === "ghost") return "transparent"
            if (control.variant === "danger") return "transparent"
            return Theme.surfaceRaised
        }

        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            color: Theme.withAlpha(Theme.text, control.down ? Theme.opacityPressed : (control.hovered ? Theme.opacityHover : 0))
            visible: control.enabled && !control.loading && (control.hovered || control.down)
        }

        Rectangle {
            anchors.fill: parent
            anchors.margins: -2
            radius: parent.radius + 2
            color: "transparent"
            border.width: control.visualFocus ? Theme.borderWidth : 0
            border.color: Theme.accent
            visible: control.visualFocus
        }
    }

    contentItem: Row {
        id: contentRow
        spacing: Theme.spacing8
        anchors.centerIn: parent
        opacity: control.loading ? Theme.opacityMuted : 1.0

        Text {
            text: control.text
            color: control.variant === "primary" ? Theme.text : (control.variant === "danger" ? Theme.error : Theme.text)
            font.pixelSize: Theme.fontBody
            font.weight: Theme.weightDemi
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
            horizontalAlignment: Text.AlignHCenter
        }
    }

    MouseArea {
        anchors.fill: parent
        enabled: control.loading
        acceptedButtons: Qt.AllButtons
        cursorShape: (control.enabled && !control.loading) ? Qt.PointingHandCursor : Qt.ArrowCursor
    }
}
