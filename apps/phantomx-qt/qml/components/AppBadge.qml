import QtQuick 2.15
import PhantomXWallet 1.0

Rectangle {
    id: badge
    property string text: ""
    property string tone: "muted" // success, warning, error, info, muted
    implicitHeight: Theme.badgeHeight
    implicitWidth: label.implicitWidth + Theme.spacing12
    radius: Theme.radiusPill
    color: {
        if (tone === "success") return Theme.withAlpha(Theme.success, 0.18)
        if (tone === "warning") return Theme.withAlpha(Theme.warning, 0.18)
        if (tone === "error") return Theme.withAlpha(Theme.error, 0.18)
        if (tone === "info") return Theme.withAlpha(Theme.info, 0.18)
        return Theme.withAlpha(Theme.textMuted, 0.14)
    }
    border.width: Theme.borderWidth
    border.color: {
        if (tone === "success") return Theme.withAlpha(Theme.success, 0.35)
        if (tone === "warning") return Theme.withAlpha(Theme.warning, 0.35)
        if (tone === "error") return Theme.withAlpha(Theme.error, 0.35)
        if (tone === "info") return Theme.withAlpha(Theme.info, 0.35)
        return Theme.border
    }

    Text {
        id: label
        anchors.centerIn: parent
        text: badge.text
        font.pixelSize: Theme.fontCaption
        font.weight: Theme.weightDemi
        color: {
            if (badge.tone === "success") return Theme.success
            if (badge.tone === "warning") return Theme.warning
            if (badge.tone === "error") return Theme.error
            if (badge.tone === "info") return Theme.info
            return Theme.textMuted
        }
    }
}
