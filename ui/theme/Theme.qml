pragma Singleton
import QtQuick

// TechieVideoEditor design tokens. Every color, font, size and duration in the UI comes
// from here; components never hard-code values. See docs/design-system.md.
QtObject {
    id: theme

    // Dark is the default: video looks most accurate against neutral dark surroundings.
    property bool dark: true

    readonly property QtObject color: QtObject {
        // Surfaces, back to front
        readonly property color canvas: theme.dark ? "#0F1115" : "#F3F4F7"        // window background
        readonly property color surface: theme.dark ? "#171A20" : "#FFFFFF"       // panels
        readonly property color surfaceRaised: theme.dark ? "#1E222A" : "#F7F8FA" // cards, headers, inputs
        readonly property color surfaceHover: theme.dark ? "#262B34" : "#EEF0F4"
        readonly property color surfaceSunken: theme.dark ? "#0B0D10" : "#E6E8ED" // player well, track lanes

        readonly property color borderSubtle: theme.dark ? "#262B34" : "#E1E4EA"
        readonly property color borderStrong: theme.dark ? "#353B46" : "#CBD0D9"

        readonly property color textPrimary: theme.dark ? "#E8EAEE" : "#161A21"
        readonly property color textSecondary: theme.dark ? "#A3AAB6" : "#5A6270"
        readonly property color textDisabled: theme.dark ? "#5E6572" : "#A1A8B4"

        // Brand accent ("Techie blue"): fills use accent, text/links on surfaces use accentText.
        readonly property color accent: theme.dark ? "#356AE6" : "#2F63DD"
        readonly property color accentHover: theme.dark ? "#2F61DA" : "#2858CC"
        readonly property color accentPressed: theme.dark ? "#2A58C7" : "#2553C2"
        readonly property color accentText: theme.dark ? "#6E95FF" : "#2A5BCC"
        readonly property color accentSubtle: theme.dark ? "#1C2A4A" : "#E3EBFD"
        readonly property color textOnAccent: "#FFFFFF"

        readonly property color success: theme.dark ? "#3CC28A" : "#1E9E6A"
        readonly property color warning: theme.dark ? "#F0B44C" : "#B87A0E"
        readonly property color danger: theme.dark ? "#F06464" : "#D23C3C"

        // Timeline
        readonly property color trackVideo: "#3D6FD9"
        readonly property color trackAudio: "#2FA77A"
        readonly property color trackTitle: "#A35CD1"
        readonly property color trackEffect: "#D98A3D"
        readonly property color playhead: theme.dark ? "#FF5A5F" : "#E0343A"
        readonly property color selection: theme.dark ? "#FFFFFF" : "#161A21"
        // Timeline markers, by the colour name the project stores
        readonly property var markers: ({
            red: "#E5484D", orange: "#E8833A", yellow: "#E2B33C",
            green: "#3CB37A", blue: "#4D8AF0", purple: "#9B6BE0"
        })

        readonly property color overlay: "#B3000000" // scrim behind text on video
    }

    // Type scale. The family is the platform UI font (SF Pro on macOS) except mono.
    readonly property QtObject type: QtObject {
        readonly property font display: Qt.font({ pixelSize: 24, weight: Font.DemiBold })
        readonly property font title: Qt.font({ pixelSize: 17, weight: Font.DemiBold })
        readonly property font heading: Qt.font({ pixelSize: 13, weight: Font.DemiBold })
        readonly property font body: Qt.font({ pixelSize: 13 })
        readonly property font label: Qt.font({ pixelSize: 12 })
        readonly property font caption: Qt.font({ pixelSize: 11 })
        readonly property font mono: Qt.font({ family: "Menlo", pixelSize: 12 })
    }

    // 4-pixel spacing grid
    readonly property QtObject space: QtObject {
        readonly property int xxs: 2
        readonly property int xs: 4
        readonly property int s: 8
        readonly property int m: 12
        readonly property int l: 16
        readonly property int xl: 24
        readonly property int xxl: 32
    }

    readonly property QtObject radius: QtObject {
        readonly property int s: 4
        readonly property int m: 6
        readonly property int l: 10
        readonly property int round: 999
    }

    // Fixed layout metrics shared by the editor regions
    readonly property QtObject size: QtObject {
        readonly property int topBar: 44
        readonly property int toolBar: 36
        readonly property int tabRail: 68
        readonly property int control: 28   // buttons, inputs
        readonly property int iconSmall: 16
        readonly property int icon: 20
        readonly property int libraryWidth: 340
        readonly property int libraryMinWidth: 260
        readonly property int inspectorWidth: 320
        readonly property int inspectorMinWidth: 280
        readonly property int playerMinWidth: 480
        readonly property int timelineHeight: 300
        readonly property int timelineMinHeight: 180
        readonly property int trackHeader: 168
        readonly property int ruler: 28
        readonly property int videoTrack: 56
        readonly property int audioTrack: 52
    }

    readonly property QtObject motion: QtObject {
        readonly property int fast: 120
        readonly property int normal: 200
    }
}
