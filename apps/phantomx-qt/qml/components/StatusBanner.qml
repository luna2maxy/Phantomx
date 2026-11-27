import QtQuick 2.15
import PhantomXWallet 1.0

Rectangle {
    id: banner
    property string text: ""
    property string tone: "warning" // warning, error, info, success
    property alias actionText: actionBtn.text
    signal actionClicked()
    visible: text.length > 0
    implicitHeight: visible ? row.implicitHeight + Theme.spacing12 * 2 : 0
    radius: Theme.radiusMedium
    border.width: Theme.borderWidth
    border.color: Theme.withAlpha(tone === "error" ? Theme.error : (tone === "info" ? Theme.info : (tone === "success" ? Theme.success : Theme.warning)), Theme.opacityBorderSoft)
    color: Theme.withAlpha(tone === "error" ? Theme.error : (tone === "info" ? Theme.info : (tone === "success" ? Theme.success : Theme.warning)), Theme.opacitySoft)

    Row {
        id: row
        anchors.fill: parent
        anchors.margins: Theme.spacing12
        spacing: Theme.spacing12

        Text {
            text: banner.text
            color: Theme.text
            font.pixelSize: Theme.fontBody
            wrapMode: Text.WordWrap
            width: Math.max(0, parent.width - (actionBtn.visible ? actionBtn.width + Theme.spacing12 : 0))
        }

        AppButton {
            id: actionBtn
            visible: text.length > 0
            size: "S"
            variant: "secondary"
            onClicked: banner.actionClicked()
        }
    }
}
