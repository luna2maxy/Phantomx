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
        contentHeight: Math.max(parent.height, content.implicitHeight + Theme.pageMargin * 2)
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: StyledScrollBar { }

        Item {
            id: content
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: Theme.pageMargin
            anchors.rightMargin: Theme.pageMargin
            y: Theme.pageMargin
            implicitHeight: mainCol.implicitHeight

            property int labelWidth: Math.min(Theme.formLabelWidth, Math.max(72, width * 0.22))

            ColumnLayout {
                id: mainCol
                width: parent.width
                spacing: Theme.spacing24

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacing12

                    SectionHeader { text: qsTr("Wallet Management") }

                    Text {
                        Layout.fillWidth: true
                        text: qsTr("Keystore: %1").arg(walletBridge ? walletBridge.keystorePath : "—")
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontCaption
                        wrapMode: Text.WrapAnywhere
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacing12

                        AppButton { text: qsTr("Create New Wallet"); variant: "primary"; size: "M"; onClicked: if (walletBridge) walletBridge.createWallet() }
                        AppButton { text: qsTr("Open/Load Wallet"); variant: "secondary"; size: "M"; onClicked: if (walletBridge) walletBridge.loadWallet() }
                        Item { Layout.fillWidth: true }
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: Theme.spacing16
                        rowSpacing: Theme.spacing12

                        Text { Layout.preferredWidth: content.labelWidth; Layout.maximumWidth: content.labelWidth; text: qsTr("Import keystore"); color: Theme.text; font.pixelSize: Theme.fontBody; elide: Text.ElideRight }
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            spacing: Theme.spacing8
                            AppButton { text: qsTr("Choose keystore file..."); variant: "secondary"; size: "S"; onClicked: if (walletBridge) walletBridge.importWalletFile() }
                            Item { Layout.fillWidth: true }
                        }

                        Text { Layout.preferredWidth: content.labelWidth; Layout.maximumWidth: content.labelWidth; text: qsTr("Backup keystore"); color: Theme.text; font.pixelSize: Theme.fontBody; elide: Text.ElideRight }
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            spacing: Theme.spacing8
                            AppButton { text: qsTr("Save backup to file..."); variant: "secondary"; size: "S"; onClicked: if (walletBridge) walletBridge.backupWalletFile() }
                            Item { Layout.fillWidth: true }
                        }

                        Text { Layout.preferredWidth: content.labelWidth; Layout.maximumWidth: content.labelWidth; text: qsTr("Treasury"); color: Theme.text; font.pixelSize: Theme.fontBody; elide: Text.ElideRight }
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            spacing: Theme.spacing8
                            AppInput { id: treasuryInput; Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.preferredHeight: Theme.controlHeight; text: walletBridge ? walletBridge.treasuryAddress : ""; placeholderText: qsTr("Treasury address") }
                            AppButton { text: qsTr("Set Treasury"); variant: "secondary"; size: "S"; onClicked: if (walletBridge) walletBridge.setTreasuryAddress(treasuryInput.text) }
                        }

                        Text { Layout.preferredWidth: content.labelWidth; Layout.maximumWidth: content.labelWidth; text: qsTr("Auto-sweep"); color: Theme.text; font.pixelSize: Theme.fontBody; elide: Text.ElideRight }
                        AppToggleSwitch { checked: walletBridge ? walletBridge.autoSweep : false; onToggled: if (walletBridge) walletBridge.setAutoSweep(checked) }
                    }
                }

                Rectangle { Layout.fillWidth: true; height: Theme.borderWidth; color: Theme.dividerColor }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacing12

                    SectionHeader { text: qsTr("Node / Network") }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: Theme.spacing16
                        rowSpacing: Theme.spacing12

                        Text { Layout.preferredWidth: content.labelWidth; Layout.maximumWidth: content.labelWidth; text: qsTr("Network"); color: Theme.text; font.pixelSize: Theme.fontBody; elide: Text.ElideRight }
                        AppComboBox {
                            id: netCombo
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            Layout.preferredHeight: Theme.controlHeight
                            model: ["mainnet", "testnet", "regtest"]
                            currentIndex: { var n = walletBridge ? walletBridge.network : "mainnet"; var i = model.indexOf(n); return i >= 0 ? i : 0 }
                        }

                        Text { Layout.preferredWidth: content.labelWidth; Layout.maximumWidth: content.labelWidth; text: qsTr("Datadir"); color: Theme.text; font.pixelSize: Theme.fontBody; elide: Text.ElideRight }
                        AppInput { id: datadirInput; Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.preferredHeight: Theme.controlHeight; text: walletBridge ? walletBridge.datadir : "" }

                        Text { Layout.preferredWidth: content.labelWidth; Layout.maximumWidth: content.labelWidth; text: qsTr("Node RPC"); color: Theme.text; font.pixelSize: Theme.fontBody; elide: Text.ElideRight }
                        AppInput { id: rpcInput; Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.preferredHeight: Theme.controlHeight; text: walletBridge ? walletBridge.nodeRpc : "" }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacing12
                        AppButton { text: qsTr("Apply Settings"); variant: "primary"; size: "M"; onClicked: if (walletBridge) walletBridge.applySettings(netCombo.currentText, datadirInput.text, rpcInput.text) }
                        Item { Layout.fillWidth: true }
                    }
                }
            }
        }
    }
}
