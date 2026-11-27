import QtQuick 2.15
import QtQuick.Controls 2.15
import PhantomXWallet 1.0

TextField {
    id: control
    implicitHeight: Theme.controlHeight
    leftPadding: Theme.spacing12
    rightPadding: Theme.spacing12
    color: Theme.text
    placeholderTextColor: Theme.textMuted
    selectionColor: Theme.accent
    selectedTextColor: Theme.textInverse
    font.pixelSize: Theme.fontBody
    hoverEnabled: true
    opacity: enabled ? 1.0 : Theme.opacityDisabled

    background: Rectangle {
        radius: Theme.radiusMedium
        color: Theme.surfaceRaised
        border.width: Theme.borderWidth
        border.color: control.activeFocus ? Theme.accent : (control.hovered ? Theme.borderStrong : Theme.border)
    }
}
