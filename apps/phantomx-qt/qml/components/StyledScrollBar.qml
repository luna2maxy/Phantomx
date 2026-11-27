import QtQuick 2.15
import QtQuick.Controls 2.15
import PhantomXWallet 1.0

ScrollBar {
    id: control
    policy: ScrollBar.AsNeeded
    minimumSize: 0.08
    padding: 2

    contentItem: Rectangle {
        implicitWidth: 6
        implicitHeight: 24
        radius: Theme.radiusPill
        color: control.pressed ? Theme.withAlpha(Theme.text, 0.35) : Theme.withAlpha(Theme.text, control.hovered ? 0.25 : 0.15)
    }

    background: Rectangle {
        implicitWidth: 10
        color: "transparent"
    }
}
