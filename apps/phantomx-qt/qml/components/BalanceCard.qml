import QtQuick 2.15
import QtQuick.Layouts 1.15
import PhantomXWallet 1.0

Rectangle {
    id: root
    property string balanceDisplay: "—"
    property string immatureBalanceDisplay: "—"
    property bool hasImmatureBalance: false
    property string network: "mainnet"
    property bool hideBalance: false
    property int toggleTopInset: -3
    property int toggleRightInset: -1
    property int toggleSize: 28
    property int toggleGap: Theme.spacing16

    signal toggleHideBalance()

    implicitHeight: Theme.balanceCardHeight
    radius: Theme.balanceCardRadius
    border.width: Theme.borderWidth
    border.color: Theme.border
    clip: false

    gradient: Gradient {
        orientation: Gradient.Horizontal
        GradientStop { position: 0.0; color: Theme.surfaceRaised }
        GradientStop { position: 1.0; color: Qt.tint(Theme.surfaceRaised, Theme.withAlpha(Theme.accent, 0.12)) }
    }

    function wholePart() {
        var s = root.hideBalance ? "••••" : (root.balanceDisplay || "—")
        if (root.hideBalance) return s
        var parts = s.replace(" PHX", "").split(".")
        return parts.length > 0 ? parts[0] : s
    }
    function fracPart() {
        if (root.hideBalance) return ""
        var s = (root.balanceDisplay || "").replace(" PHX", "")
        var parts = s.split(".")
        return parts.length > 1 ? "." + parts[1] : ""
    }
    function unitPart() { return root.hideBalance ? "" : "PHX" }

    // Subtle motif inside card
    Text {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.rightMargin: Theme.spacing12
        anchors.bottomMargin: -Theme.spacing8
        text: "X"
        color: Theme.withAlpha(Theme.text, 0.07)
        font.pixelSize: Theme.fontMotif
        font.weight: Theme.weightBold
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacing16
        spacing: Theme.spacing8

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing8

            Image {
                Layout.preferredWidth: 24
                Layout.preferredHeight: 24
                source: "qrc:/PhantomXWallet/assets/logo.png"
                fillMode: Image.PreserveAspectFit
                smooth: true
                mipmap: true
            }
            Text {
                text: "PHANTOMX"
                color: Theme.text
                font.pixelSize: Theme.fontCaption
                font.weight: Theme.weightBold
                elide: Text.ElideRight
            }
            Item { Layout.fillWidth: true }

            Rectangle {
                visible: root.network.length > 0 && root.network !== "mainnet"
                radius: Theme.radiusPill
                color: Theme.withAlpha(Theme.error, 0.18)
                border.width: Theme.borderWidth
                border.color: Theme.withAlpha(Theme.error, 0.35)
                implicitHeight: 20
                implicitWidth: netLabel.implicitWidth + Theme.spacing12
                // Reserve a full-spacing gap before the top-right hide/show toggle.
                Layout.rightMargin: root.toggleSize + root.toggleRightInset + root.toggleGap - Theme.spacing16
                Text { id: netLabel; anchors.centerIn: parent; text: root.network; color: Theme.error; font.pixelSize: Theme.fontCaption; font.weight: Theme.weightDemi }
            }
        }

        Text {
            text: qsTr("Balance")
            color: Theme.textMuted
            font.pixelSize: Theme.fontCaption
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing4

            Text {
                Layout.maximumWidth: root.width * 0.45
                text: root.wholePart()
                color: Theme.text
                font.pixelSize: Theme.fontBalance
                font.weight: Theme.weightBold
                elide: Text.ElideRight
            }
            Text {
                visible: root.fracPart().length > 0
                text: root.fracPart()
                color: Theme.text
                font.pixelSize: Theme.fontBody
                font.weight: Theme.weightDemi
                Layout.alignment: Qt.AlignBottom
                Layout.bottomMargin: 4
                elide: Text.ElideRight
            }
            Text {
                visible: root.unitPart().length > 0
                text: root.unitPart()
                color: Theme.textMuted
                font.pixelSize: Theme.fontCaption
                font.weight: Theme.weightDemi
                Layout.alignment: Qt.AlignBottom
                Layout.bottomMargin: 5
            }
            Item { Layout.fillWidth: true }
        }

        Text {
            visible: root.hasImmatureBalance
            text: root.hideBalance ? qsTr("Immature: ••••") : qsTr("Immature: %1").arg(root.immatureBalanceDisplay)
            color: Theme.textMuted
            font.pixelSize: Theme.fontCaption
            elide: Text.ElideRight
            Layout.fillWidth: true
        }

        Item { Layout.fillHeight: true }
    }

    IconButton {
        id: toggle
        z: 2
        anchors.top: parent.top
        anchors.topMargin: root.toggleTopInset
        anchors.right: parent.right
        anchors.rightMargin: root.toggleRightInset
        width: root.toggleSize
        height: root.toggleSize
        iconText: root.hideBalance ? "◌" : "●"
        tooltipText: root.hideBalance ? qsTr("Show balance") : qsTr("Hide balance")
        onClicked: root.toggleHideBalance()
    }
}
