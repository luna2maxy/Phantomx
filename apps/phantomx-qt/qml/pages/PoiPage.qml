import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import PhantomXWallet 1.0
import "../components"

Item {
    id: page
    property bool nodeOffline: walletBridge ? !walletBridge.online : true

    function rowsFor(groupName) {
        var src = walletBridge ? walletBridge.poiStatusRows : []
        var out = []
        for (var i = 0; i < src.length; i++) {
            if (src[i].group === groupName) out.push(src[i])
        }
        return out
    }

    Flickable {
        id: flick
        anchors.fill: parent
        contentWidth: width
        contentHeight: contentCol.implicitHeight + Theme.pageMargin * 2
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: StyledScrollBar { }

        ColumnLayout {
            id: contentCol
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.leftMargin: Theme.pageMargin
            anchors.rightMargin: Theme.pageMargin
            anchors.topMargin: Theme.pageMargin
            spacing: Theme.cardGap

            StatusBanner {
                Layout.fillWidth: true
                visible: page.nodeOffline
                tone: "warning"
                text: page.nodeOffline ? qsTr("Node offline, data not available. Showing local data if available.") : ""
                actionText: qsTr("Try Again")
                onActionClicked: if (walletBridge) walletBridge.refresh()
            }

            AppCard {
                Layout.fillWidth: true
                compact: true

                Text { text: qsTr("PoI"); color: Theme.text; font.pixelSize: Theme.fontSubtitle; font.weight: Theme.weightBold }

                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: Theme.spacing16
                    rowSpacing: Theme.spacing12

                    Text { Layout.preferredWidth: Theme.formLabelWidth; text: qsTr("Enable PoI"); color: Theme.text; font.pixelSize: Theme.fontBody; elide: Text.ElideRight }
                    AppToggleSwitch { id: enableSwitch; checked: walletBridge ? walletBridge.poiEnabled : false }

                    Text { Layout.preferredWidth: Theme.formLabelWidth; text: qsTr("Bandwidth limit"); color: Theme.text; font.pixelSize: Theme.fontBody; elide: Text.ElideRight }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacing8
                        AppSpinBox {
                            id: bwSpin
                            Layout.fillWidth: true
                            Layout.preferredHeight: Theme.controlHeight
                            from: 1; to: 1048576
                            value: walletBridge ? walletBridge.poiBandwidthValue : 100
                        }
                        AppComboBox {
                            id: bwUnit
                            Layout.preferredWidth: Theme.inputUnitWidth
                            Layout.preferredHeight: Theme.controlHeight
                            model: ["MB", "GB", "TB"]
                            currentIndex: { var u = walletBridge ? walletBridge.poiBandwidthUnit : "MB"; var idx = model.indexOf(u); return idx >= 0 ? idx : 0 }
                        }
                    }

                    Text { Layout.preferredWidth: Theme.formLabelWidth; text: qsTr("Domain / Verifier"); color: Theme.text; font.pixelSize: Theme.fontBody; elide: Text.ElideRight }
                    AppInput {
                        id: domainInput
                        Layout.fillWidth: true
                        Layout.preferredHeight: Theme.controlHeight
                        placeholderText: qsTr("verifier.example.org:9000")
                        text: walletBridge ? walletBridge.poiDomain : ""
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacing12
                    AppButton { text: qsTr("Start PoI"); variant: "primary"; size: "M"; enabled: walletBridge ? !walletBridge.poiRunning : false; onClicked: if (walletBridge) walletBridge.startPoi() }
                    AppButton { text: qsTr("Stop PoI"); variant: "danger"; size: "M"; enabled: walletBridge ? walletBridge.poiRunning : false; onClicked: if (walletBridge) walletBridge.stopPoi() }
                    AppButton { text: qsTr("Apply Settings"); variant: "secondary"; size: "M"; onClicked: if (walletBridge) walletBridge.applyPoiSettings(enableSwitch.checked, bwSpin.value, bwUnit.currentText, domainInput.text) }
                    Item { Layout.fillWidth: true }
                }
            }

            AppCard {
                Layout.fillWidth: true
                compact: true

                Text { text: qsTr("Status"); color: Theme.text; font.pixelSize: Theme.fontSubtitle; font.weight: Theme.weightBold }
                Text { text: qsTr("Connection"); color: Theme.textMuted; font.pixelSize: Theme.fontCaption; font.weight: Theme.weightDemi }
                Repeater {
                    model: page.rowsFor("Connection")
                    delegate: StatusRow { Layout.fillWidth: true; label: modelData.label; value: modelData.value; badge: modelData.badge || ""; badgeTone: modelData.badgeTone || "muted"; dimmed: page.nodeOffline && modelData.value === "—" }
                }

                Rectangle { Layout.fillWidth: true; height: Theme.borderWidth; color: Theme.dividerColor }

                Text { text: qsTr("Performance"); color: Theme.textMuted; font.pixelSize: Theme.fontCaption; font.weight: Theme.weightDemi }
                Repeater {
                    model: page.rowsFor("Performance")
                    delegate: StatusRow { Layout.fillWidth: true; label: modelData.label; value: modelData.value; badge: modelData.badge || ""; badgeTone: modelData.badgeTone || "muted"; dimmed: page.nodeOffline && modelData.value === "—" }
                }

                Rectangle { Layout.fillWidth: true; height: Theme.borderWidth; color: Theme.dividerColor }

                Text { text: qsTr("Configuration"); color: Theme.textMuted; font.pixelSize: Theme.fontCaption; font.weight: Theme.weightDemi }
                Repeater {
                    model: page.rowsFor("Configuration")
                    delegate: StatusRow { Layout.fillWidth: true; label: modelData.label; value: modelData.value; badge: modelData.badge || ""; badgeTone: modelData.badgeTone || "muted"; dimmed: false }
                }
            }

            AppCard {
                Layout.fillWidth: true
                compact: true
                Text { text: qsTr("Backend"); color: Theme.text; font.pixelSize: Theme.fontSubtitle; font.weight: Theme.weightBold }
                Text { Layout.fillWidth: true; text: walletBridge ? walletBridge.poiBackendNote : "—"; color: Theme.textMuted; font.pixelSize: Theme.fontBody; wrapMode: Text.WordWrap }
            }

            Item { Layout.preferredHeight: Theme.spacing16 }
        }
    }
}
