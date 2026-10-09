import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import BoardGui

ApplicationWindow {
    id: window
    width: Theme.screenWidth
    height: Theme.screenHeight
    visible: true
    title: "Board GUI"
    color: Theme.background
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fontBody

    // ---- Navigation -------------------------------------------------------
    // One place that knows about every screen. Screens call
    // window.navigateTo("race") etc. instead of pushing components themselves.
    readonly property var routes: ({
        "setup":    setupScreen,
        "sensor":   sensorScreen,
        "race":     raceScreen,
        "settings": settingsScreen
    })

    // Which screen is showing, so the SideBar can highlight it. Stack depth
    // alone can't tell us, because navigateTo replaces rather than pushes.
    property string currentRoute: "home"

    function navigateTo(name) {
        if (name === "home") { goHome(); return }
        if (name === currentRoute) return   // re-tapping the active item shouldn't reset the screen
        const comp = routes[name]
        if (!comp) {
            console.warn("Unknown route:", name)
            return
        }
        // If we're already deeper than home, replace instead of stacking forever
        if (stack.depth > 1)
            stack.replace(stack.currentItem, comp)
        else
            stack.push(comp)
        currentRoute = name
    }

    function goHome() { stack.pop(null); currentRoute = "home" }   // pop back to MainScreen
    function goBack() {
        if (stack.depth > 1) stack.pop()
        if (stack.depth === 1) currentRoute = "home"
    }

    // Sidebar on the left, page header + screens on the right.
    // (Replaces the old `header: NavBar` - the sidebar now owns navigation,
    // so there's no Back/Home button bar eating vertical space.)
    RowLayout {
        anchors.fill: parent
        spacing: 0

        SideBar {
            Layout.fillHeight: true
            Layout.preferredWidth: Theme.sidebarWidth
            currentRoute: window.currentRoute
            onNavigate: (route) => window.navigateTo(route)
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            PageHeader {
                Layout.fillWidth: true
                title: stack.currentItem && stack.currentItem.title ? stack.currentItem.title : ""
                subtitle: stack.currentItem && stack.currentItem.subtitle ? stack.currentItem.subtitle : ""
            }

            StackView {
                id: stack
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                initialItem: mainScreen

                // Quick crossfade instead of the default slide: with a sidebar
                // there is no "direction" to slide in, and fades feel calmer.
                pushEnter:    Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.animNormal; easing.type: Theme.easing } }
                pushExit:     Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.animFast } }
                popEnter:     Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.animNormal; easing.type: Theme.easing } }
                popExit:      Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.animFast } }
                replaceEnter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.animNormal; easing.type: Theme.easing } }
                replaceExit:  Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.animFast } }
            }
        }
    }

    // Escape / hardware back button
    Shortcut { sequences: [StandardKey.Back, "Esc"]; onActivated: window.goBack() }

    // ---- Screen components --------------------------------------------------
    Component { id: mainScreen;     MainScreen     { onOpenScreen: (name) => window.navigateTo(name) } }
    Component { id: setupScreen;    SetupScreen    { onStartRace: window.navigateTo("race") } }
    Component { id: sensorScreen;   SensorScreen   {} }
    Component { id: raceScreen;     RaceScreen     {} }
    Component { id: settingsScreen; SettingsScreen {} }
}
