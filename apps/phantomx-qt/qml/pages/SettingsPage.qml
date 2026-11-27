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

                Flow {
                    Layout.fillWidth: true
                    spacing: Theme.spacing12
                    AppButton { text: qsTr("Create New Wallet"); variant: "primary"; size: "M"; onClicked: if (walletBridge) walletBridge.createWallet() }
                    AppButton { text: qsTr("Open/Load Wallet"); variant: "secondary"; size: "M"; onClicked: if (walletBridge) walletBridge.loadWallet() }
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: Theme.spacing16
                    rowSpacing: Theme.spacing12

                    Text { Layout.preferredWidth: Theme.formLabelWidth; text: qsTr("Import secret hex"); color: Theme.text; font.pixelSize: Theme.fontBody; elide: Text.ElideRight }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacing8
                        AppInput { id: importInput; Layout.fillWidth: true; placeholderText: qsTr("Local only, never sent to server"); echoMode: TextInput.Password }
                        AppButton { text: qsTr("Import"); variant: "secondary"; size: "M"; onClicked: { if (walletBridge) { walletBridge.importWallet(importInput.text); importInput.text = "" } } }
                    }

                    Text { Layout.preferredWidth: Theme.formLabelWidth; text: qsTr("Backup path"); color: Theme.text; font.pixelSize: Theme.fontBody; elide: Text.ElideRight }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacing8
                        AppInput { id: backupInput; Layout.fillWidth: true; placeholderText: qsTr("/path/to/wallet.keystore.bak") }
                        AppButton { text: qsTr("Backup"); variant: "secondary"; size: "M"; onClicked: if (walletBridge) walletBridge.backupWallet(backupInput.text) }
                    }

                    Text { Layout.preferredWidth: Theme.formLabelWidth; text: qsTr("Treasury"); color: Theme.text; font.pixelSize: Theme.fontBody; elide: Text.ElideRight }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacing8
                        AppInput { id: treasuryInput; Layout.fillWidth: true; text: walletBridge ? walletBridge.treasuryAddress : ""; placeholderText: qsTr("Treasury address") }
                        AppButton { text: qsTr("Set Treasury"); variant: "secondary"; size: "M"; onClicked: if (walletBridge) walletBridge.setTreasuryAddress(treasuryInput.text) }
                    }

                    Text { Layout.preferredWidth: Theme.formLabelWidth; text: qsTr("Auto-sweep"); color: Theme.text; font.pixelSize: Theme.fontBody; elide: Text.ElideRight }
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

                    Text { Layout.preferredWidth: Theme.formLabelWidth; text: qsTr("Network"); color: Theme.text; font.pixelSize: Theme.fontBody; elide: Text.ElideRight }
                    AppComboBox {
                        id: netCombo
                        Layout.fillWidth: true
                        Layout.preferredHeight: Theme.controlHeight
                        model: ["mainnet", "testnet", "regtest"]
                        currentIndex: { var n = walletBridge ? walletBridge.network : "mainnet"; var i = model.indexOf(n); return i >= 0 ? i : 0 }
                    }

                    Text { Layout.preferredWidth: Theme.formLabelWidth; text: qsTr("Datadir"); color: Theme.text; font.pixelSize: Theme.fontBody; elide: Text.ElideRight }
                    AppInput { id: datadirInput; Layout.fillWidth: true; text: walletBridge ? walletBridge.datadir : "" }

                    Text { Layout.preferredWidth: Theme.formLabelWidth; text: qsTr("Node RPC"); color: Theme.text; font.pixelSize: Theme.fontBody; elide: Text.ElideRight }
                    AppInput { id: rpcInput; Layout.fillWidth: true; text: walletBridge ? walletBridge.nodeRpc : "" }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacing12
                    AppButton { text: qsTr("Apply Settings"); variant: "primary"; size: "M"; onClicked: if (walletBridge) walletBridge.applySettings(netCombo.currentText, datadirInput.text, rpcInput.text) }
                    Item { Layout.fillWidth: true }
                }
            }

            Item { Layout.preferredHeight: Theme.spacing16 }
        }
    }
}
