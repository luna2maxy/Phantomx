import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import PhantomXWallet 1.0
import "../components"

Item {
    id: page
    property string resultText: qsTr("No transaction sent yet.")
    property bool resultError: false

    Flickable {
        id: flick
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

            SectionHeader { text: qsTr("Send") }

            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: Theme.spacing16
                rowSpacing: Theme.spacing12

                Text { Layout.preferredWidth: Theme.formLabelWidth; text: qsTr("To"); color: Theme.text; font.pixelSize: Theme.fontBody; elide: Text.ElideRight }
                AppInput { id: toInput; Layout.fillWidth: true; placeholderText: qsTr("PhantomX destination address") }

                Text { Layout.preferredWidth: Theme.formLabelWidth; text: qsTr("Amount (atoms)"); color: Theme.text; font.pixelSize: Theme.fontBody; elide: Text.ElideRight }
                AppInput { id: amountInput; Layout.fillWidth: true; placeholderText: qsTr("100000000 = 1 PHX") }

                Text { Layout.preferredWidth: Theme.formLabelWidth; text: qsTr("Fee preset"); color: Theme.text; font.pixelSize: Theme.fontBody; elide: Text.ElideRight }
                AppComboBox {
                    id: feeCombo
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.controlHeight
                    model: [qsTr("Fast (2 conf)"), qsTr("Normal (6 conf)"), qsTr("Slow (12 conf)")]
                    currentIndex: 1
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacing12

                AppButton {
                    text: qsTr("Send Transaction")
                    variant: "primary"
                    size: "M"
                    enabled: (walletBridge ? walletBridge.online : false) && toInput.text.trim().length > 0 && amountInput.text.trim().length > 0
                    onClicked: confirmDialog.open()
                }

                Text {
                    Layout.fillWidth: true
                    text: walletBridge && !walletBridge.online ? qsTr("Node offline - send unavailable") : ""
                    color: Theme.warning
                    font.pixelSize: Theme.fontCaption
                    wrapMode: Text.WordWrap
                }
            }

            Text {
                Layout.fillWidth: true
                text: page.resultText
                color: page.resultError ? Theme.error : Theme.textMuted
                font.pixelSize: Theme.fontBody
                wrapMode: Text.WordWrap
            }

            Item { Layout.preferredHeight: Theme.spacing16 }
        }
    }

    ConfirmDialog {
        id: confirmDialog
        title: qsTr("Send Transaction")
        message: qsTr("Send transaction to the destination address? Make sure the address and amount are correct.")
        confirmText: qsTr("Send")
        cancelText: qsTr("Cancel")
        onConfirmed: {
            if (!walletBridge) return
            var res = walletBridge.sendToAddress(toInput.text, amountInput.text, feeCombo.currentIndex)
            if (res.indexOf("OK:") === 0) { page.resultError = false; page.resultText = qsTr("TXID: %1").arg(res.substring(3)) }
            else { page.resultError = true; page.resultText = res.indexOf("ERR:") === 0 ? res.substring(4) : res }
        }
    }
}
