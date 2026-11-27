pragma Singleton
import QtQuick 2.15

QtObject {
    // Radius
    readonly property int radiusSmall: 4
    readonly property int radiusMedium: 8
    readonly property int radiusLarge: 12
    readonly property int radiusPill: 999

    // Spacing
    readonly property int spacing4: 4
    readonly property int spacing8: 8
    readonly property int spacing12: 12
    readonly property int spacing16: 16
    readonly property int spacing24: 24
    readonly property int spacing32: 32

    // Border
    readonly property int borderWidth: 1
    readonly property color border: "#404040"
    readonly property color borderStrong: "#5a5a5a"

    // Icons
    readonly property int iconSmall: 16
    readonly property int iconMedium: 20
    readonly property int iconLarge: 24

    // Typography
    readonly property int fontTitle: 20
    readonly property int fontSubtitle: 16
    readonly property int fontBody: 14
    readonly property int fontCaption: 12
    readonly property int fontBalance: 28
    readonly property int fontMotif: 96
    readonly property int weightBold: Font.Bold
    readonly property int weightDemi: Font.DemiBold
    readonly property int weightNormal: Font.Normal
    readonly property string fontMono: "monospace"

    // Opacity
    readonly property real opacityDisabled: 0.4
    readonly property real opacityHover: 0.08
    readonly property real opacityPressed: 0.16
    readonly property real opacityScrim: 0.5
    readonly property real opacityMuted: 0.7
    readonly property real opacityDivider: 0.45
    readonly property real opacitySoft: 0.12
    readonly property real opacityBorderSoft: 0.35

    // Animation
    readonly property int durationFast: 120
    readonly property int durationNormal: 200

    // Background layers
    readonly property color bg: "#2d2d2d"
    readonly property color surface: "#333333"
    readonly property color surfaceRaised: "#3a3a3a"
    readonly property color overlay: "#242424"

    // Text
    readonly property color text: "#f2f2f2"
    readonly property color textMuted: "#c2c2c2"
    readonly property color textInverse: "#1a1a1a"

    // Accent / status
    readonly property color accent: "#F26822"
    readonly property color accentHover: "#ff7a35"
    readonly property color success: "#4CAF7A"
    readonly property color warning: "#E0A458"
    readonly property color error: "#E06C6C"
    readonly property color info: "#5AA9E6"

    // Layout / control tokens
    readonly property int pageMargin: 24
    readonly property int controlHeight: 36
    readonly property int buttonHeightS: 28
    readonly property int buttonHeightM: 36
    readonly property int buttonHeightL: 44
    readonly property int formLabelWidth: 180
    readonly property int navItemHeight: 40
    readonly property int statusRowHeight: 28
    readonly property int sidebarWidth: 232
    readonly property int titleBarHeight: 50
    readonly property int statusDotSize: 8
    readonly property int navActiveMarkerWidth: 3
    readonly property int badgeHeight: 22
    readonly property int logoSize: 34
    readonly property int switchTrackWidth: 52
    readonly property int switchTrackHeight: 28
    readonly property int switchKnobSize: 22
    readonly property int switchKnobMargin: 3
    readonly property int spinIndicatorSize: 28
    readonly property int inputUnitWidth: 100

    // Card / list tokens
    readonly property int cardPadding: 16
    readonly property int cardPaddingCompact: 12
    readonly property int cardGap: 12
    readonly property int cardRadius: 8
    readonly property int rowHeight: 52
    readonly property int rowPaddingX: 16
    readonly property int listLabelWidth: 120
    readonly property color dividerColor: border
    readonly property color rowHoverColor: withAlpha(text, 0.06)
    readonly property int balanceCardHeight: 140
    readonly property int balanceCardRadius: 12

    function withAlpha(c, a) { return Qt.rgba(c.r, c.g, c.b, a) }
}
