import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import PhantomXWallet 1.0
import "../components"

Item {
    id: page

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.pageMargin
        spacing: Theme.spacing16

        Text {
            Layout.fillWidth: true
            text: qsTr("A single keystore can contain multiple addresses. The treasury is the primary receiving address.")
            color: Theme.textMuted
            font.pixelSize: Theme.fontCaption
            wrapMode: Text.WordWrap
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: Math.max(56, mainAddrRow.implicitHeight + Theme.cardPaddingCompact * 2)
            radius: Theme.cardRadius
            color: Theme.surface
            border.width: Theme.borderWidth
            border.color: Theme.border

            RowLayout {
                id: mainAddrRow
                anchors.fill: parent
                anchors.margins: Theme.cardPaddingCompact
                spacing: Theme.spacing12

                Text {
                    Layout.fillWidth: true
                    text: walletBridge && walletBridge.treasuryAddress.length > 0 ? walletBridge.treasuryAddress : qsTr("No wallet loaded")
                    color: Theme.text
                    font.pixelSize: Theme.fontBody
                    font.family: Theme.fontMono
                    elide: Text.ElideMiddle
                    verticalAlignment: Text.AlignVCenter

                    MouseArea { id: addrHover; anchors.fill: parent; hoverEnabled: true; acceptedButtons: Qt.NoButton }
                    AppTooltip { text: walletBridge ? walletBridge.treasuryAddress : ""; visible: addrHover.containsMouse && text.length > 0 }
                }

                AppButton {
                    Layout.alignment: Qt.AlignVCenter
                    text: qsTr("Copy Address")
                    variant: "secondary"
                    size: "S"
                    onClicked: { if (walletBridge) { walletBridge.copyText(walletBridge.treasuryAddress); toast.show(qsTr("Address copied"), "success") } }
                }
            }
        }

        Flow {
            Layout.fillWidth: true
            Layout.preferredHeight: implicitHeight
            spacing: Theme.spacing12
            AppButton { text: qsTr("Generate New Address"); variant: "secondary"; size: "S"; onClicked: if (walletBridge) walletBridge.generateNewAddress() }
            AppButton { text: qsTr("Generate 5 Addresses"); variant: "secondary"; size: "S"; onClicked: if (walletBridge) walletBridge.generateBatchAddresses(5) }
        }

        Text {
            Layout.fillWidth: true
            text: qsTr("Addresses in Keystore")
            color: Theme.text
            font.pixelSize: Theme.fontSubtitle
            font.weight: Theme.weightBold
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.surface
            radius: Theme.cardRadius
            border.width: Theme.borderWidth
            border.color: Theme.border

            ListView {
                id: addrList
                anchors.fill: parent
                anchors.margins: Theme.borderWidth
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                model: walletBridge ? walletBridge.addressList : []
                spacing: 0
                ScrollBar.vertical: StyledScrollBar { }

                delegate: Item {
                    width: addrList.width
                    implicitHeight: Math.max(Theme.rowHeight, row.implicitHeight + Theme.spacing12 * 2)
                    height: implicitHeight

                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: 2
                        radius: Theme.radiusSmall
                        color: rowMouse.containsMouse ? Theme.rowHoverColor : "transparent"
                    }

                    RowLayout {
                        id: row
                        anchors.fill: parent
                        anchors.leftMargin: Theme.rowPaddingX
                        anchors.rightMargin: Theme.rowPaddingX
                        spacing: Theme.spacing12

                        Text {
                            Layout.preferredWidth: Theme.listLabelWidth
                            Layout.alignment: Qt.AlignVCenter
                            text: modelData.label || qsTr("Address")
                            color: Theme.text
                            font.pixelSize: Theme.fontBody
                            font.weight: Theme.weightDemi
                            elide: Text.ElideRight
                            verticalAlignment: Text.AlignVCenter
                        }

                        Text {
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignVCenter
                            text: modelData.address || "—"
                            color: Theme.textMuted
                            font.pixelSize: Theme.fontCaption
                            font.family: Theme.fontMono
                            elide: Text.ElideMiddle
                            verticalAlignment: Text.AlignVCenter
                            MouseArea { id: rowAddrHover; anchors.fill: parent; hoverEnabled: true; acceptedButtons: Qt.NoButton }
                            AppTooltip { text: modelData.address || ""; visible: rowAddrHover.containsMouse && text.length > 0 }
                        }

                        IconButton {
                            Layout.alignment: Qt.AlignVCenter
                            iconText: "⧉"
                            tooltipText: qsTr("Copy address")
                            onClicked: { if (walletBridge) { walletBridge.copyText(modelData.address); toast.show(qsTr("Address copied"), "success") } }
                        }
                    }

                    Rectangle {
                        visible: index < (addrList.count - 1)
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.leftMargin: Theme.rowPaddingX
                        anchors.rightMargin: Theme.rowPaddingX
                        height: Theme.borderWidth
                        color: Theme.dividerColor
                    }

                    MouseArea { id: rowMouse; anchors.fill: parent; hoverEnabled: true; acceptedButtons: Qt.NoButton }
                }

                EmptyState {
                    anchors.centerIn: parent
                    width: parent.width - Theme.spacing32
                    visible: addrList.count === 0
                    title: qsTr("No addresses")
                    message: qsTr("Generate a new address to receive funds.")
                }
            }
        }
    }

    Toast {
        id: toast
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.spacing24
        z: 100
    }
}
