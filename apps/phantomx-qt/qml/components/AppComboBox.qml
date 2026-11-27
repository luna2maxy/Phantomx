import QtQuick 2.15
import QtQuick.Controls 2.15
import PhantomXWallet 1.0

ComboBox {
    id: control
    implicitHeight: Theme.controlHeight
    hoverEnabled: true
    opacity: enabled ? 1.0 : Theme.opacityDisabled
    font.pixelSize: Theme.fontBody

    background: Rectangle {
        radius: Theme.radiusMedium
        color: Theme.surfaceRaised
        border.width: Theme.borderWidth
        border.color: control.activeFocus ? Theme.accent : (control.hovered ? Theme.borderStrong : Theme.border)
    }

    contentItem: Text {
        leftPadding: Theme.spacing12
        rightPadding: Theme.spacing24
        text: control.displayText
        font: control.font
        color: Theme.text
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    indicator: Text {
        x: control.width - width - Theme.spacing12
        y: (control.height - height) / 2
        text: "▾"
        color: Theme.textMuted
        font.pixelSize: Theme.fontBody
    }

    popup: Popup {
        y: control.height + Theme.spacing4
        width: control.width
        padding: Theme.spacing4
        contentItem: ListView {
            clip: true
            implicitHeight: Math.min(contentHeight, 200)
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            ScrollIndicator.vertical: ScrollIndicator { }
        }
        background: Rectangle {
            radius: Theme.radiusMedium
            color: Theme.surfaceRaised
            border.width: Theme.borderWidth
            border.color: Theme.border
        }
    }

    delegate: ItemDelegate {
        width: control.width
        height: Theme.controlHeight
        contentItem: Text {
            text: modelData
            color: Theme.text
            font.pixelSize: Theme.fontBody
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
            leftPadding: Theme.spacing8
        }
        background: Rectangle {
            radius: Theme.radiusSmall
            color: highlighted ? Theme.withAlpha(Theme.accent, 0.18) : "transparent"
        }
    }
}
