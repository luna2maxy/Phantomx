import QtQuick 2.15
import QtQuick.Controls 2.15
import PhantomXWallet 1.0

Switch {
    id: control
    implicitHeight: Theme.controlHeight
    implicitWidth: Theme.switchTrackWidth
    hoverEnabled: true
    opacity: enabled ? 1.0 : Theme.opacityDisabled

    indicator: Rectangle {
        implicitWidth: Theme.switchTrackWidth
        implicitHeight: Theme.switchTrackHeight
        x: control.leftPadding
        y: parent.height / 2 - height / 2
        radius: Theme.radiusPill
        color: control.checked ? Theme.accent : Theme.surfaceRaised
        border.width: Theme.borderWidth
        border.color: control.checked ? Theme.accent : Theme.border

        Rectangle {
            width: Theme.switchKnobSize
            height: Theme.switchKnobSize
            radius: Theme.radiusPill
            y: Theme.switchKnobMargin
            x: control.checked ? parent.width - width - Theme.switchKnobMargin : Theme.switchKnobMargin
            color: Theme.text
            Behavior on x { NumberAnimation { duration: Theme.durationFast } }
        }
    }

    contentItem: Item {}
}
