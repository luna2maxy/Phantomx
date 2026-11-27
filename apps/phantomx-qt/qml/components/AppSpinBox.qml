import QtQuick 2.15
import QtQuick.Controls 2.15
import PhantomXWallet 1.0

SpinBox {
    id: control
    implicitHeight: Theme.controlHeight
    hoverEnabled: true
    opacity: enabled ? 1.0 : Theme.opacityDisabled
    editable: true
    from: 0
    to: 999999

    contentItem: TextInput {
        z: 2
        text: control.textFromValue(control.value, control.locale)
        font.pixelSize: Theme.fontBody
        color: Theme.text
        selectionColor: Theme.accent
        selectedTextColor: Theme.textInverse
        horizontalAlignment: Qt.AlignHCenter
        verticalAlignment: Qt.AlignVCenter
        readOnly: !control.editable
        validator: control.validator
        inputMethodHints: Qt.ImhDigitsOnly
        onTextEdited: {
            var v = parseInt(text)
            if (!isNaN(v)) control.value = Math.max(control.from, Math.min(control.to, v))
        }
    }

    up.indicator: Rectangle {
        x: control.width - width - Theme.spacing4
        y: (control.height - height) / 2
        implicitWidth: Theme.spinIndicatorSize
        implicitHeight: Theme.spinIndicatorSize
        radius: Theme.radiusSmall
        color: control.up.pressed ? Theme.withAlpha(Theme.text, Theme.opacityPressed) : "transparent"
        Text { anchors.centerIn: parent; text: "+"; color: Theme.text; font.pixelSize: Theme.fontBody; font.weight: Theme.weightBold }
    }

    down.indicator: Rectangle {
        x: Theme.spacing4
        y: (control.height - height) / 2
        implicitWidth: Theme.spinIndicatorSize
        implicitHeight: Theme.spinIndicatorSize
        radius: Theme.radiusSmall
        color: control.down.pressed ? Theme.withAlpha(Theme.text, Theme.opacityPressed) : "transparent"
        Text { anchors.centerIn: parent; text: "−"; color: Theme.text; font.pixelSize: Theme.fontBody; font.weight: Theme.weightBold }
    }

    background: Rectangle {
        radius: Theme.radiusMedium
        color: Theme.surfaceRaised
        border.width: Theme.borderWidth
        border.color: control.activeFocus ? Theme.accent : (control.hovered ? Theme.borderStrong : Theme.border)
    }
}
