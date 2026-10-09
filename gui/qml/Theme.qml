pragma Singleton
import QtQuick

// Design tokens. Replace these values with the ones from your Figma file
// (colors, text styles, spacing) so every screen picks them up automatically.
QtObject {
    // Screen size of the board's display
    readonly property int screenWidth: 800
    readonly property int screenHeight: 480

    // Colors
    readonly property color background:   "#0F1115"
    readonly property color surface:      "#1A1D24"
    readonly property color surfaceHover: "#232733"
    readonly property color border:       "#2C313D"
    readonly property color textPrimary:  "#F2F4F8"
    readonly property color textMuted:    "#8A93A6"
    readonly property color accent:       "#4F8CFF"
    readonly property color success:      "#3DD68C"
    readonly property color warning:      "#F5B83D"
    readonly property color danger:       "#FF5C5C"

    // Premium layering: same palette, just finer tonal steps between layers.
    // Dark UIs read as "expensive" through subtle depth, not loud shadows.
    readonly property color surfaceLow:   "#16191F"    // bottom of card gradients
    readonly property color surfaceRaised:"#20242D"    // cards sitting on cards
    readonly property color borderSubtle: "#992C313D"  // border at ~60% alpha (#AARRGGBB)
    readonly property color highlight:    "#0FFFFFFF"  // ~6% white, 1px top "glass" edge

    // Accent at a given opacity, for tinted chips/backgrounds instead of solid blocks.
    function tint(c, alpha) { return Qt.rgba(c.r, c.g, c.b, alpha) }

    // Typography
    readonly property string fontFamily: "Inter"   // falls back if not bundled
    readonly property int fontSmall:   14
    readonly property int fontBody:    16
    readonly property int fontTitle:   22
    readonly property int fontDisplay: 72

    // Spacing / shape
    readonly property int spacingXs: 4
    readonly property int spacingSm: 8
    readonly property int spacingMd: 16
    readonly property int spacingLg: 24
    readonly property int radius: 12
    readonly property int radiusSm: 8
    readonly property int radiusLg: 16
    readonly property int navBarHeight: 56   // old top NavBar (no longer used)
    readonly property int sidebarWidth: 184  // leaves 616px for screen content
    readonly property int navItemHeight: 52
    readonly property int touchTarget: 48   // minimum tappable size

    // Motion: short and eased so presses feel responsive, not floaty
    readonly property int animFast: 120
    readonly property int animNormal: 180
    readonly property int easing: Easing.OutCubic
}
