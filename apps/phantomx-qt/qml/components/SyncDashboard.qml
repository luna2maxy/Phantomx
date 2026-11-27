import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import PhantomXWallet 1.0

Rectangle {
    id: root
    color: "transparent"
    implicitHeight: walletSection.implicitHeight + nodeSection.implicitHeight + Theme.spacing8

    property bool walletExpanded: false
    property bool nodeExpanded: false
    property int nowMs: Date.now()

    Component.onCompleted: root.walletExpanded = root.height >= 180

    Timer {
        interval: 1000
        running: true
        repeat: true
        onTriggered: root.nowMs = Date.now()
    }

    function groupNumber(v) {
        const n = Math.max(0, Math.floor(Number(v || 0)))
        return String(n).replace(/\B(?=(\d{3})+(?!\d))/g, ",")
    }

    function heightText(v) {
        const n = Number(v || 0)
        return n > 0 ? "#" + groupNumber(n) : "—"
    }

    function countText(v) {
        const n = Number(v || 0)
        return n > 0 ? groupNumber(n) : "0"
    }

    function percentText(v, valid) {
        if (!valid || v < 0) return "—"
        return (Number(v) * 100).toFixed(2) + "%"
    }

    function rateText(v) {
        const n = Number(v)
        if (!isFinite(n) || n <= 0) return "Calculating..."
        return (n >= 10 ? n.toFixed(0) : n.toFixed(1)) + " blocks/s"
    }

    function etaText(seconds) {
        const n = Number(seconds)
        if (!isFinite(n) || n < 0) return "Calculating..."
        if (n < 60) return Math.ceil(n) + " seconds"
        const totalMinutes = Math.floor(n / 60)
        const sec = Math.ceil(n % 60)
        if (totalMinutes < 60) {
            return totalMinutes + " min " + sec + " sec"
        }
        const hours = Math.floor(totalMinutes / 60)
        const minutes = totalMinutes % 60
        return hours + " hour" + (hours === 1 ? "" : "s") + " " + minutes + " min"
    }

    function agoText(ms) {
        const t = Number(ms || 0)
        if (t <= 0) return "—"
        const seconds = Math.max(0, Math.floor((root.nowMs - t) / 1000))
        if (seconds < 5) return "Just now"
        if (seconds < 60) return seconds + " seconds ago"
        const minutes = Math.floor(seconds / 60)
        if (minutes < 60) return minutes + " minute" + (minutes === 1 ? "" : "s") + " ago"
        const hours = Math.floor(minutes / 60)
        return hours + " hour" + (hours === 1 ? "" : "s") + " ago"
    }

    function metric(label, value, tone) {
        return { label: label, value: value, tone: tone || "", fullWidth: false }
    }

    function message(text, tone) {
        return { label: "", value: text, tone: tone || "error", fullWidth: true }
    }

    function walletState() {
        if (!walletBridge) return { text: "Unavailable", tone: "info", animating: false, valid: false }
        if (!walletBridge.online && !walletBridge.usingLocalData) {
            return { text: "Offline", tone: "error", animating: false, valid: false }
        }
        if (!walletBridge.walletSyncAvailable) {
            return { text: "Waiting", tone: "warning", animating: false, valid: false }
        }
        if (walletBridge.walletSyncing) {
            return { text: "Waiting", tone: "info", animating: true, valid: false }
        }
        if (walletBridge.usingLocalData && !walletBridge.online) {
            return { text: "Local Data", tone: "info", animating: false, valid: false }
        }
        return { text: "Synced", tone: "success", animating: false, valid: true }
    }

    function nodeState() {
        if (!walletBridge) return { text: "Unavailable", tone: "info", animating: false, valid: false }
        if (!walletBridge.online && !walletBridge.usingLocalData) {
            return { text: "Offline", tone: "error", animating: false, valid: false }
        }
        if (walletBridge.nodeSyncing) {
            return { text: "Syncing", tone: "accent", animating: true, valid: walletBridge.nodeSyncProgress >= 0 }
        }
        if (walletBridge.usingLocalData && !walletBridge.online) {
            return { text: "Local Data", tone: "info", animating: false, valid: false }
        }
        return { text: "Online", tone: "success", animating: false, valid: true }
    }

    Flickable {
        anchors.fill: parent
        contentWidth: width
        contentHeight: dashColumn.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        ScrollBar.vertical: ScrollBar {
            width: 4
            rightPadding: 0
            leftPadding: 0
            policy: ScrollBar.AsNeeded
            contentItem: Rectangle {
                radius: 2
                color: Theme.withAlpha(Theme.accent, 0.55)
            }
        }

        ColumnLayout {
            id: dashColumn
            width: parent.width
            spacing: Theme.spacing8

            SyncSection {
                id: walletSection
                Layout.fillWidth: true
                title: qsTr("Wallet Sync")
                statusText: {
                    const s = root.walletState()
                    return s.text
                }
                statusTone: {
                    const s = root.walletState()
                    return s.tone
                }
                animating: root.walletState().animating
                progressText: {
                    const s = root.walletState()
                    return percentText(walletBridge ? walletBridge.walletSyncProgress : -1, s.valid)
                }
                progressValue: walletBridge ? walletBridge.walletSyncProgress : 0
                progressValid: root.walletState().valid
                expanded: root.walletExpanded
                onToggleExpanded: {
                    root.walletExpanded = !root.walletExpanded
                    if (root.walletExpanded) root.nodeExpanded = false
                }
                metrics: {
                    const live = walletBridge && walletBridge.online
                    const indexed = walletBridge ? walletBridge.walletScanHeight : 0
                    const tip = walletBridge ? walletBridge.walletScanTargetHeight : 0
                    const rows = [
                        metric(qsTr("Current block"), tip > 0 ? (live ? heightText(tip) : qsTr("Last known %1").arg(heightText(tip))) : (live ? qsTr("Waiting for first block") : "—"), tip > 0 || live ? "" : "error"),
                        metric(qsTr("Target block"), tip > 0 ? heightText(tip) : "—"),
                        metric(qsTr("Remaining"), live && tip > 0 ? "0 blocks" : "—"),
                        metric(qsTr("Scan speed"), live ? rateText(walletBridge.walletBlocksPerSecond) : "—", live && walletBridge.walletBlocksPerSecond > 0 ? "info" : ""),
                        metric(qsTr("Estimated time"), live ? etaText(walletBridge.walletEtaSeconds) : "—", live && walletBridge.walletEtaSeconds >= 0 ? "info" : ""),
                        metric(qsTr("Last indexed block"), indexed > 0 ? heightText(indexed) : "—", "info"),
                        metric(qsTr("Last scanned"), agoText(walletBridge ? walletBridge.walletLastScanMs : 0), "info")
                    ]
                    if (!live && walletBridge && !walletBridge.online && !walletBridge.usingLocalData) {
                        rows.push(message(qsTr("Live data: Waiting for node connection"), "error"))
                    }
                    return rows
                }
            }

            SyncSection {
                id: nodeSection
                Layout.fillWidth: true
                title: qsTr("Node Sync")
                statusText: {
                    const s = root.nodeState()
                    return s.text
                }
                statusTone: {
                    const s = root.nodeState()
                    return s.tone
                }
                animating: root.nodeState().animating
                progressText: {
                    const s = root.nodeState()
                    return percentText(walletBridge ? walletBridge.nodeSyncProgress : -1, s.valid)
                }
                progressValue: walletBridge ? walletBridge.nodeSyncProgress : 0
                progressValid: root.nodeState().valid
                progressLabel: qsTr("Blockchain sync")
                expanded: root.nodeExpanded
                onToggleExpanded: {
                    root.nodeExpanded = !root.nodeExpanded
                    if (root.nodeExpanded) root.walletExpanded = false
                }
                metrics: {
                    const live = walletBridge && walletBridge.online
                    const current = walletBridge ? walletBridge.nodeHeight : 0
                    const target = walletBridge ? walletBridge.networkHeight : 0
                    const remaining = Math.max(0, target - current)
                    const rows = [
                        metric(qsTr("Node height"), current > 0 ? heightText(current) : (live ? "Waiting for first block" : "Last known: —"), current > 0 ? "" : "error"),
                        metric(qsTr("Network height"), target > 0 ? heightText(target) : "Target unavailable"),
                        metric(qsTr("Remaining"), live && target > 0 ? countText(remaining) + " blocks" : "—"),
                        metric(qsTr("Block speed"), live ? rateText(walletBridge.nodeBlocksPerSecond) : "—", live && walletBridge.nodeBlocksPerSecond > 0 ? "info" : ""),
                        metric(qsTr("Estimated time"), live ? etaText(walletBridge.nodeEtaSeconds) : "—", live && walletBridge.nodeEtaSeconds >= 0 ? "info" : ""),
                        metric(qsTr("Connected peers"), walletBridge ? countText(walletBridge.connectionCount) : "0", walletBridge && walletBridge.connectionCount > 0 ? "success" : "error"),
                        metric(qsTr("Last block"), agoText(walletBridge ? walletBridge.nodeLastBlockMs : 0), "info"),
                        metric(qsTr("Last successful sync"), agoText(walletBridge ? walletBridge.nodeLastSyncMs : 0), "info")
                    ]
                    if (!live && walletBridge && !walletBridge.online && !walletBridge.usingLocalData) {
                        rows.push(message(qsTr("Live data: Waiting for node connection"), "error"))
                    }
                    return rows
                }
            }
        }
    }
}
