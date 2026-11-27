import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import PhantomXWallet 1.0
import "shell"
import "pages"

ApplicationWindow {
    id: window
    visible: false
    width: 1180
    height: 760
    minimumWidth: 980
    minimumHeight: 640
    title: "PhantomX Wallet"
    color: Theme.bg

    property int currentPage: 0
    property var navItems: [
        { label: qsTr("Overview"), component: overviewComp },
        { label: qsTr("Send"), component: sendComp },
        { label: qsTr("Receive"), component: receiveComp },
        { label: qsTr("Transactions"), component: transactionsComp },
        { label: qsTr("PoI"), component: poiComp },
        { label: qsTr("Settings"), component: settingsComp }
    ]

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Sidebar {
            Layout.preferredWidth: Theme.sidebarWidth
            Layout.fillHeight: true
            currentIndex: window.currentPage
            navModel: window.navItems
            onNavigate: function(index) { window.currentPage = index }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            TopBar {
                Layout.fillWidth: true
                Layout.preferredHeight: 56
                pageTitle: window.navItems[window.currentPage].label
                online: walletBridge ? walletBridge.online : false
                usingLocalData: walletBridge ? walletBridge.usingLocalData : false
            }

            Loader {
                Layout.fillWidth: true
                Layout.fillHeight: true
                sourceComponent: window.navItems[window.currentPage].component
            }
        }
    }

    Component { id: overviewComp; OverviewPage {} }
    Component { id: sendComp; SendPage {} }
    Component { id: receiveComp; ReceivePage {} }
    Component { id: transactionsComp; TransactionsPage {} }
    Component { id: poiComp; PoiPage {} }
    Component { id: settingsComp; SettingsPage {} }
}
