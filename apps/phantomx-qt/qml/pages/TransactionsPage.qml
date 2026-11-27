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

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing12
            Text {
                Layout.fillWidth: true
                text: qsTr("Transactions")
                color: Theme.text
                font.pixelSize: Theme.fontSubtitle
                font.weight: Theme.weightBold
                elide: Text.ElideRight
            }
            AppButton { text: qsTr("Refresh"); variant: "ghost"; size: "S"; onClicked: if (walletBridge) walletBridge.refresh() }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.surface
            radius: Theme.cardRadius
            border.width: Theme.borderWidth
            border.color: Theme.border

            ListView {
                id: txList
                anchors.fill: parent
                anchors.margins: Theme.borderWidth
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                model: walletBridge ? walletBridge.transactions : []
                spacing: 0
                ScrollBar.vertical: StyledScrollBar { }

                delegate: Column {
                    id: txDelegate
                    width: txList.width
                    property bool expanded: false

                    Item {
                        width: parent.width
                        height: Math.max(Theme.rowHeight, summaryRow.implicitHeight + Theme.spacing12 * 2)

                        Rectangle {
                            anchors.fill: parent
                            anchors.margins: 2
                            radius: Theme.radiusSmall
                            color: summaryMouse.containsMouse ? Theme.rowHoverColor : "transparent"
                        }

                        RowLayout {
                            id: summaryRow
                            anchors.fill: parent
                            anchors.leftMargin: Theme.rowPaddingX
                            anchors.rightMargin: Theme.rowPaddingX
                            spacing: Theme.spacing12

                            Text { text: txDelegate.expanded ? "▾" : "▸"; color: Theme.textMuted; font.pixelSize: Theme.fontBody; Layout.preferredWidth: Theme.iconSmall; horizontalAlignment: Text.AlignHCenter }
                            Text { text: modelData.spent === "yes" ? "↑" : "↓"; color: modelData.spent === "yes" ? Theme.textMuted : Theme.success; font.pixelSize: Theme.fontBody; Layout.preferredWidth: Theme.iconSmall; horizontalAlignment: Text.AlignHCenter }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Text { Layout.fillWidth: true; text: (modelData.spent === "yes" ? qsTr("Spent") : qsTr("Received")) + "  " + (modelData.amount || "—"); color: Theme.text; font.pixelSize: Theme.fontBody; font.weight: Theme.weightDemi; elide: Text.ElideRight }
                                Text { Layout.fillWidth: true; text: qsTr("Block %1").arg(modelData.height || "—"); color: Theme.textMuted; font.pixelSize: Theme.fontCaption; elide: Text.ElideRight }
                            }

                            Text { Layout.preferredWidth: 120; text: modelData.account || "—"; color: Theme.textMuted; font.pixelSize: Theme.fontCaption; elide: Text.ElideRight; horizontalAlignment: Text.AlignRight }
                        }

                        MouseArea { id: summaryMouse; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: txDelegate.expanded = !txDelegate.expanded }
                    }

                    Item {
                        width: parent.width
                        visible: txDelegate.expanded
                        height: visible ? detailCol.implicitHeight + Theme.spacing12 * 2 : 0

                        Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.leftMargin: Theme.rowPaddingX; anchors.rightMargin: Theme.rowPaddingX; height: Theme.borderWidth; color: Theme.dividerColor; opacity: Theme.opacityDivider }

                        ColumnLayout {
                            id: detailCol
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.leftMargin: Theme.rowPaddingX + Theme.iconSmall + Theme.spacing12
                            anchors.rightMargin: Theme.rowPaddingX
                            anchors.topMargin: Theme.spacing12
                            spacing: Theme.spacing8
                            Text { Layout.fillWidth: true; text: qsTr("Transaction ID: %1").arg(modelData.txid || "—"); color: Theme.textMuted; font.pixelSize: Theme.fontCaption; font.family: Theme.fontMono; wrapMode: Text.WrapAnywhere }
                            Text { Layout.fillWidth: true; text: qsTr("Fee: %1").arg("—"); color: Theme.textMuted; font.pixelSize: Theme.fontCaption }
                            Text { Layout.fillWidth: true; text: qsTr("Blockheight: %1").arg(modelData.height || "—"); color: Theme.textMuted; font.pixelSize: Theme.fontCaption }
                            Text { Layout.fillWidth: true; text: qsTr("Account: %1").arg(modelData.account || "—"); color: Theme.textMuted; font.pixelSize: Theme.fontCaption; wrapMode: Text.WrapAnywhere }
                        }
                    }

                    Rectangle {
                        width: parent.width - Theme.rowPaddingX * 2
                        x: Theme.rowPaddingX
                        height: Theme.borderWidth
                        color: Theme.dividerColor
                        visible: index < (txList.count - 1)
                    }
                }

                EmptyState {
                    anchors.centerIn: parent
                    width: parent.width - Theme.spacing32
                    visible: txList.count === 0
                    title: walletBridge && walletBridge.online ? qsTr("No transactions") : (walletBridge && walletBridge.usingLocalData ? qsTr("No local transactions found") : qsTr("Not Available"))
                    message: walletBridge && !walletBridge.online && !walletBridge.usingLocalData ? qsTr("Node offline, data not available.") : ""
                }

                footer: Item {
                    width: txList.width
                    height: txList.count > 0 ? 36 : 0
                    Text {
                        anchors.centerIn: parent
                        visible: txList.count > 0
                        text: qsTr("No more results")
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontCaption
                    }
                }
            }
        }
    }
}
