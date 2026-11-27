import QtQuick 2.15
import QtQuick.Controls 2.15
import PhantomXWallet 1.0

ToolTip {
    id: control
    delay: 400
    timeout: 5000
    padding: Theme.spacing8
    visible: parent && parent.hovered && text.length > 0

    contentItem: Text {
        text: control.text
        color: Theme.text
        font.pixelSize: Theme.fontCaption
        wrapMode: Text.WordWrap
    }

    background: Rectangle {
        color: Theme.surfaceRaised
        border.width: Theme.borderWidth
        border.color: Theme.border
        radius: Theme.radiusSmall
    }
}
