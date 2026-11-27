import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import PhantomXWallet 1.0

Popup {
    id: root
    property string title: qsTr("Confirm")
    property string message: ""
    property string confirmText: qsTr("Confirm")
    property string cancelText: qsTr("Cancel")
    property bool danger: false
    signal confirmed()
    signal cancelled()

    modal: true
    focus: true
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    anchors.centerIn: Overlay.overlay
    width: Math.min(480, Overlay.overlay ? Overlay.overlay.width - Theme.spacing32 * 2 : 480)

    Overlay.modal: Rectangle {
        color: Theme.withAlpha("#000000", Theme.opacityScrim)
    }

    background: Rectangle {
        color: Theme.surfaceRaised
        radius: Theme.radiusLarge
        border.width: Theme.borderWidth
        border.color: Theme.border
    }

    contentItem: ColumnLayout {
        spacing: Theme.spacing16
        anchors.margins: Theme.spacing24

        Item { Layout.preferredHeight: Theme.spacing8 }

        Text {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacing24
            Layout.rightMargin: Theme.spacing24
            text: root.title
            color: Theme.text
            font.pixelSize: Theme.fontSubtitle
            font.weight: Theme.weightBold
            wrapMode: Text.WordWrap
        }

        Text {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacing24
            Layout.rightMargin: Theme.spacing24
            text: root.message
            color: Theme.textMuted
            font.pixelSize: Theme.fontBody
            wrapMode: Text.WordWrap
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacing24
            Layout.rightMargin: Theme.spacing24
            Layout.bottomMargin: Theme.spacing24
            spacing: Theme.spacing12
            Item { Layout.fillWidth: true }
            AppButton { text: root.cancelText; variant: "secondary"; size: "M"; onClicked: { root.cancelled(); root.close() } }
            AppButton { text: root.confirmText; variant: root.danger ? "danger" : "primary"; size: "M"; onClicked: { root.confirmed(); root.close() } }
        }
    }
}
