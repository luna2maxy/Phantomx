import QtQuick 2.15
import QtQuick.Layouts 1.15
import PhantomXWallet 1.0

Rectangle {
    id: root
    property string text: ""
    property string tone: "info" // info, success, warning, error
    property int durationMs: 2400

    visible: opacity > 0
    opacity: 0
    radius: Theme.radiusMedium
    color: Theme.surfaceRaised
    border.width: Theme.borderWidth
    border.color: {
        if (tone === "success") return Theme.withAlpha(Theme.success, Theme.opacityBorderSoft)
        if (tone === "warning") return Theme.withAlpha(Theme.warning, Theme.opacityBorderSoft)
        if (tone === "error") return Theme.withAlpha(Theme.error, Theme.opacityBorderSoft)
        return Theme.withAlpha(Theme.info, Theme.opacityBorderSoft)
    }
    implicitHeight: msg.implicitHeight + Theme.spacing12 * 2
    implicitWidth: Math.min(520, msg.implicitWidth + Theme.spacing16 * 2)

    function show(msgText, msgTone) {
        root.text = msgText || ""
        root.tone = msgTone || "info"
        opacity = 1
        timer.restart()
    }

    Behavior on opacity { NumberAnimation { duration: Theme.durationNormal } }

    Text {
        id: msg
        anchors.fill: parent
        anchors.margins: Theme.spacing12
        text: root.text
        color: Theme.text
        font.pixelSize: Theme.fontBody
        wrapMode: Text.WordWrap
        verticalAlignment: Text.AlignVCenter
    }

    Timer {
        id: timer
        interval: root.durationMs
        onTriggered: root.opacity = 0
    }
}
