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

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacing16
        spacing: Theme.spacing12

        BalanceCard {
            Layout.fillWidth: true
            balanceDisplay: walletBridge ? walletBridge.balanceDisplay : "—"
            immatureBalanceDisplay: walletBridge ? walletBridge.immatureBalanceDisplay : "—"
            hasImmatureBalance: walletBridge ? walletBridge.hasImmatureBalance : false
            hideBalance: walletBridge ? walletBridge.hideBalance : false
            network: walletBridge ? walletBridge.network : ""
            onToggleHideBalance: if (walletBridge) walletBridge.setHideBalance(!walletBridge.hideBalance)
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

        Item {
            Layout.fillWidth: true
            Layout.topMargin: -10
            Layout.preferredHeight: syncDashboard.implicitHeight
            Layout.maximumHeight: Math.max(130, root.height * 0.42 + 10)
            clip: true

            SyncDashboard {
                id: syncDashboard
                anchors.fill: parent
            }
        }
    }
}
