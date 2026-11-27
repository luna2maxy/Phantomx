import QtQuick 2.15
import QtQuick.Layouts 1.15
import PhantomXWallet 1.0
import "../components"

Rectangle {
    id: root
    color: Theme.overlay
    border.width: 0

    property int currentIndex: 0
    property var navModel: []
    signal navigate(int index)

    function networkStatusText() {
        if (!walletBridge) return qsTr("Not Available")
        if (walletBridge.online) return qsTr("Connected")
        if (walletBridge.usingLocalData) return qsTr("Offline (Local Data)")
        return qsTr("Disconnected")
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacing16
        spacing: Theme.spacing12

        BalanceCard {
            Layout.fillWidth: true
            balanceDisplay: walletBridge ? walletBridge.balanceDisplay : "—"
            immatureBalanceDisplay: walletBridge ? walletBridge.immatureBalanceDisplay : "—"
            hasImmatureBalance: walletBridge ? walletBridge.hasImmatureBalance : false
            network: walletBridge ? walletBridge.network : ""
        }

        Item { Layout.preferredHeight: Theme.spacing4 }

        Repeater {
            model: root.navModel
            delegate: Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.navItemHeight
                radius: Theme.radiusMedium
                color: root.currentIndex === index ? Theme.surfaceRaised : (mouse.containsMouse ? Theme.withAlpha(Theme.text, Theme.opacityHover) : "transparent")

                Rectangle {
                    visible: root.currentIndex === index
                    width: Theme.navActiveMarkerWidth
                    radius: Theme.radiusSmall
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    anchors.margins: Theme.spacing4
                    color: Theme.accent
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.spacing16
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.spacing12
                    text: modelData.label
                    color: root.currentIndex === index ? Theme.accent : Theme.text
                    font.pixelSize: Theme.fontBody
                    font.weight: Theme.weightDemi
                    elide: Text.ElideRight
                }

                MouseArea {
                    id: mouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.navigate(index)
                }
            }
        }

        Item { Layout.fillHeight: true }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing8

            Text { text: qsTr("Network status"); color: Theme.textMuted; font.pixelSize: Theme.fontCaption }
            Text {
                Layout.fillWidth: true
                text: root.networkStatusText()
                color: Theme.text
                font.pixelSize: Theme.fontBody
                font.weight: Theme.weightDemi
                elide: Text.ElideRight
            }

            Text { text: qsTr("Wallet sync"); color: Theme.textMuted; font.pixelSize: Theme.fontCaption }
            Rectangle {
                Layout.fillWidth: true
                height: 4
                radius: 2
                color: Theme.withAlpha(Theme.text, Theme.opacitySoft)
                Rectangle {
                    width: parent.width * (walletBridge ? Math.max(0, Math.min(1, walletBridge.syncProgress)) : 0)
                    height: parent.height
                    radius: 2
                    color: Theme.accent
                }
            }
            Text {
                Layout.fillWidth: true
                text: walletBridge ? (walletBridge.syncing ? qsTr("Synchronizing...") : (walletBridge.online || walletBridge.usingLocalData ? qsTr("Synced") : "—")) : "—"
                color: Theme.textMuted
                font.pixelSize: Theme.fontCaption
                elide: Text.ElideRight
            }

            Text { text: qsTr("Node sync"); color: Theme.textMuted; font.pixelSize: Theme.fontCaption }
            Rectangle {
                Layout.fillWidth: true
                height: 4
                radius: 2
                color: Theme.withAlpha(Theme.text, Theme.opacitySoft)
                Rectangle {
                    width: {
                        if (!walletBridge) return 0
                        var h = walletBridge.chainHeight
                        var t = walletBridge.headerHeight
                        if (t > 0) return parent.width * Math.max(0, Math.min(1, h / t))
                        return (walletBridge.online || walletBridge.usingLocalData) ? parent.width : 0
                    }
                    height: parent.height
                    radius: 2
                    color: Theme.accent
                }
            }
            Text {
                Layout.fillWidth: true
                text: {
                    if (!walletBridge) return "—"
                    if (!walletBridge.online && !walletBridge.usingLocalData) return "—"
                    var h = walletBridge.chainHeight
                    var t = walletBridge.headerHeight
                    if (t > 0) return h + " / " + t
                    if (h > 0) return String(h)
                    return "—"
                }
                color: Theme.textMuted
                font.pixelSize: Theme.fontCaption
                elide: Text.ElideRight
            }
        }
    }
}
