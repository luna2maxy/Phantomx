import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import PhantomXWallet 1.0
import "../components"

Item {
    id: page

    Flickable {
        anchors.fill: parent
        contentWidth: width
        contentHeight: col.implicitHeight + Theme.pageMargin * 2
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: StyledScrollBar { }

        ColumnLayout {
            id: col
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.leftMargin: Theme.pageMargin
            anchors.rightMargin: Theme.pageMargin
            anchors.topMargin: Theme.pageMargin
            spacing: Theme.spacing16

            SectionHeader { text: qsTr("Overview") }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: overviewCol.implicitHeight + Theme.cardPaddingCompact * 2
                radius: Theme.cardRadius
                color: Theme.surface
                border.width: Theme.borderWidth
                border.color: Theme.border

                ColumnLayout {
                    id: overviewCol
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: Theme.cardPaddingCompact
                    spacing: Theme.spacing12

                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Balance: %1").arg(walletBridge ? walletBridge.balanceDisplay : "—")
                        color: Theme.text
                        font.pixelSize: Theme.fontSubtitle
                        font.weight: Theme.weightBold
                        wrapMode: Text.WordWrap
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: walletBridge ? walletBridge.hasImmatureBalance : false
                        text: qsTr("Immature: %1").arg(walletBridge ? walletBridge.immatureBalanceDisplay : "—")
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontBody
                        wrapMode: Text.WordWrap
                    }

                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Address: %1").arg(walletBridge && walletBridge.address.length > 0 ? walletBridge.address : "—")
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontBody
                        font.family: Theme.fontMono
                        wrapMode: Text.WrapAnywhere
                    }

                    AppButton {
                        text: qsTr("Copy Address")
                        variant: "secondary"
                        size: "S"
                        onClicked: if (walletBridge) walletBridge.copyAddress()
                    }
                }
            }

            Item { Layout.preferredHeight: Theme.spacing16 }
        }
    }
}
