import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import TechieVideoEditor.Theme

// Made for you: an instant video (pictures and videos in a style), a highlight montage (the
// best moments of videos) or smart shorts (tall clips of the best moments of one long video,
// following the subject, with captions). Each is a new project; the media is measured first.
Popup {
    id: panel
    signal importRequested() // (no media yet: the panel's Import button)
    required property var project
    property string mode: "instant" // "instant", "montage", "shorts"
    property var chosen: []          // media ids, in order
    property string style: "upbeat"
    property string shape: "16:9"
    property int seconds: 30
    property int count: 3
    property string result: ""       // what the last one said
    property bool working: false

    readonly property var media: project.media.filter((m) => m.hasVideo && !m.sticker && !m.missing && !m.placeholder
                                                              && (mode === "instant" || !m.still))
    readonly property bool canRank: project.cloud.hasClaudeKey
    readonly property bool hasWords: mode === "shorts" && chosen.length === 1 && project.hasTranscript(chosen[0])

    function show(which) {
        mode = which
        result = ""
        chosen = mode === "shorts" ? (media.length > 0 ? [media[0].id] : []) : media.map((m) => m.id)
        seconds = mode === "shorts" ? 30 : 30
        shape = mode === "shorts" ? "9:16" : "16:9"
        open()
    }
    function toggle(id) {
        if (mode === "shorts") {
            chosen = [id]
            return
        }
        const next = chosen.slice()
        const at = next.indexOf(id)
        if (at >= 0)
            next.splice(at, 1)
        else
            next.push(id)
        chosen = next
    }
    function make() {
        result = ""
        const started = mode === "shorts"
            ? project.makeShorts({ media: chosen[0], count: count, seconds: seconds, captions: captions.checked, rank: rank.checked })
            : project.makeAutoEdit({ kind: mode, media: chosen, style: style, shape: shape, seconds: seconds,
                                     title: title.text, rank: rank.checked })
        working = started
    }

    Connections {
        target: panel.project
        function onAutoEditFinished(kind, ok, message) {
            panel.working = false
            panel.result = !ok ? "" : message ? qsTr("Done · %1").arg(message) : qsTr("Done")
        }
        // Stopped or failed while measuring.
        function onAnalysisFinished(ok, message) { if (!ok) panel.working = false }
    }
    FolderDialog {
        id: exportFolder
        title: qsTr("Export each short into")
        onAccepted: panel.project.exportMarkerRanges(selectedFolder)
    }

    component Chip: AbstractButton {
        id: chip
        property string label
        hoverEnabled: true
        leftPadding: Theme.space.m
        rightPadding: Theme.space.m
        implicitHeight: 26
        background: Rectangle {
            radius: height / 2
            color: chip.checked ? Theme.color.accentSubtle : chip.hovered ? Theme.color.surfaceHover : "transparent"
            border.color: chip.checked ? Theme.color.accent : Theme.color.borderSubtle
        }
        contentItem: Text {
            text: chip.label
            font: Theme.type.caption
            color: chip.checked ? Theme.color.accentText : Theme.color.textSecondary
            verticalAlignment: Text.AlignVCenter
        }
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    width: 560
    height: Math.min(implicitHeight, (parent ? parent.height : 800) - Theme.space.xl * 2)
    padding: Theme.space.l
    closePolicy: working ? Popup.NoAutoClose : Popup.CloseOnEscape | Popup.CloseOnPressOutside

    background: Rectangle {
        radius: Theme.radius.l
        color: Theme.color.surfaceRaised
        border.color: Theme.color.borderStrong
    }

    contentItem: ColumnLayout {
        spacing: Theme.space.s

        RowLayout {
            Layout.fillWidth: true
            Repeater {
                model: [{ id: "instant", label: qsTr("Instant video") }, { id: "montage", label: qsTr("Highlight montage") },
                        { id: "shorts", label: qsTr("Smart shorts") }]
                delegate: Chip {
                    required property var modelData
                    objectName: "autoMode:" + modelData.id
                    label: modelData.label
                    checked: panel.mode === modelData.id
                    enabled: !panel.working
                    onClicked: panel.show(modelData.id)
                }
            }
            Item { Layout.fillWidth: true }
            IconButton { iconName: "close"; tip: qsTr("Close"); enabled: !panel.working; onClicked: panel.close() }
        }
        Text {
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            text: panel.mode === "instant" ? qsTr("Pictures and the best moments of videos, cut to the beat of a style's music.")
                : panel.mode === "montage" ? qsTr("The best moments of your videos — loud, lively or spoken — cut to the beat.")
                : qsTr("Tall shorts of the best moments of one long video, following the subject, each ready to export on its own.")
            font: Theme.type.body
            color: Theme.color.textSecondary
            wrapMode: Text.WordWrap
        }

        Text { text: panel.mode === "shorts" ? qsTr("The video") : qsTr("From"); font: Theme.type.label; color: Theme.color.textSecondary }
        Flow {
            Layout.fillWidth: true
            spacing: Theme.space.xs
            Repeater {
                model: panel.media
                delegate: Chip {
                    required property var modelData
                    objectName: "autoMedia:" + modelData.name
                    label: modelData.name
                    checked: panel.chosen.indexOf(modelData.id) >= 0
                    enabled: !panel.working
                    onClicked: panel.toggle(modelData.id)
                }
            }
            Text {
                visible: panel.media.length === 0
                text: panel.mode === "instant" ? qsTr("Import some pictures or videos first.") : qsTr("Import some videos first.")
                font: Theme.type.caption
                color: Theme.color.textDisabled
            }
            TextButton {
                objectName: "autoEditImport"
                visible: panel.media.length === 0
                text: qsTr("Import media…")
                iconName: "import"
                onClicked: panel.importRequested()
            }
        }

        // Style and shape (instant video and montage)
        Text { visible: panel.mode !== "shorts"; text: qsTr("Style"); font: Theme.type.label; color: Theme.color.textSecondary }
        GridLayout {
            visible: panel.mode !== "shorts"
            Layout.fillWidth: true
            columns: 3
            columnSpacing: Theme.space.xs
            rowSpacing: Theme.space.xs
            Repeater {
                model: panel.project.autoEditStyles
                delegate: AbstractButton {
                    id: styleCard
                    required property var modelData
                    objectName: "autoStyle:" + modelData.id
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    implicitHeight: 58
                    checked: panel.style === modelData.id
                    hoverEnabled: true
                    enabled: !panel.working
                    onClicked: panel.style = modelData.id
                    ToolTip.visible: hovered
                    ToolTip.delay: 600
                    ToolTip.text: modelData.description
                    background: Rectangle {
                        radius: Theme.radius.m
                        color: styleCard.checked ? Theme.color.accentSubtle : styleCard.hovered ? Theme.color.surfaceHover : Theme.color.surfaceSunken
                        border.color: styleCard.checked ? Theme.color.accent : Theme.color.borderSubtle
                    }
                    contentItem: RowLayout {
                        spacing: Theme.space.s
                        Image {
                            visible: styleCard.modelData.look !== ""
                            Layout.preferredWidth: 48
                            Layout.preferredHeight: 27
                            Layout.leftMargin: Theme.space.xs
                            source: styleCard.modelData.look ? "image://tvelook/" + styleCard.modelData.look : ""
                            fillMode: Image.PreserveAspectCrop
                        }
                        Text {
                            Layout.fillWidth: true
                            Layout.leftMargin: styleCard.modelData.look ? 0 : Theme.space.s
                            text: styleCard.modelData.name
                            font: Theme.type.label
                            color: styleCard.checked ? Theme.color.accentText : Theme.color.textPrimary
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }
        Flow {
            visible: panel.mode !== "shorts"
            Layout.fillWidth: true
            spacing: Theme.space.xs
            Repeater {
                model: panel.project.templates.shapes
                delegate: Chip {
                    required property var modelData
                    objectName: "autoShape:" + modelData.id
                    label: modelData.id
                    checked: panel.shape === modelData.id
                    enabled: !panel.working
                    onClicked: panel.shape = modelData.id
                }
            }
        }

        // Length (and how many shorts)
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.space.xs
            Text { text: panel.mode === "shorts" ? qsTr("Each about") : qsTr("About"); font: Theme.type.label; color: Theme.color.textSecondary }
            Repeater {
                model: panel.mode === "shorts" ? [15, 30, 60] : [15, 30, 60, 120]
                delegate: Chip {
                    required property int modelData
                    objectName: "autoSeconds:" + modelData
                    label: modelData < 60 ? qsTr("%1 s").arg(Fmt.num(modelData)) : qsTr("%1 min").arg(Fmt.num(modelData / 60))
                    checked: panel.seconds === modelData
                    enabled: !panel.working
                    onClicked: panel.seconds = modelData
                }
            }
            Item { Layout.fillWidth: true }
            Text { visible: panel.mode === "shorts"; text: qsTr("How many"); font: Theme.type.label; color: Theme.color.textSecondary }
            SpinBox {
                id: countBox
                objectName: "shortsCount"
                visible: panel.mode === "shorts"
                from: 1
                to: 5
                value: panel.count
                enabled: !panel.working
                onValueModified: panel.count = value
            }
        }

        TextField {
            id: title
            objectName: "autoTitle"
            visible: panel.mode !== "shorts"
            Layout.fillWidth: true
            placeholderText: qsTr("An opening title (optional)")
            color: Theme.color.textPrimary
            placeholderTextColor: Theme.color.textDisabled
            selectByMouse: true
            enabled: !panel.working
            background: Rectangle {
                implicitHeight: 32
                radius: Theme.radius.s
                color: Theme.color.surfaceSunken
                border.color: parent.activeFocus ? Theme.color.accent : Theme.color.borderSubtle
            }
        }
        ToggleSwitch {
            id: captions
            objectName: "shortsCaptions"
            visible: panel.mode === "shorts"
            text: panel.hasWords ? qsTr("Captions from its transcript") : qsTr("Captions (transcribe the video first, in the Transcript tab)")
            checked: true
            enabled: panel.hasWords && !panel.working
        }
        ToggleSwitch {
            id: rank
            objectName: "autoRank"
            text: panel.canRank ? (panel.mode === "shorts" ? qsTr("Let Claude choose the order and titles") : qsTr("Let Claude choose the order"))
                                : qsTr("Let Claude choose (add a Claude key in AI settings)")
            checked: false
            enabled: panel.canRank && !panel.working
        }

        Text {
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            text: qsTr("This starts a new project in place of the current one; save that first if you want to keep it.")
            font: Theme.type.caption
            color: Theme.color.textSecondary
            wrapMode: Text.WordWrap
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.space.s
            Text {
                objectName: "autoStatus"
                Layout.fillWidth: true
                text: panel.working ? (panel.project.analysisTask || panel.project.cloud.status || qsTr("Working…"))
                                      + (panel.project.analyzing ? " " + Fmt.percent(panel.project.analysisProgress) : "")
                                    : panel.result
                font: Theme.type.caption
                color: Theme.color.textSecondary
                elide: Text.ElideRight
            }
            TextButton {
                objectName: "exportShorts"
                visible: !panel.working && panel.mode === "shorts" && panel.result !== ""
                text: qsTr("Export each short…")
                variant: "secondary"
                onClicked: exportFolder.open()
            }
            TextButton {
                objectName: "cancelAutoEdit"
                visible: panel.working
                text: qsTr("Stop")
                variant: "ghost"
                onClicked: { panel.project.cancelAnalysis(); panel.project.cloud.cancel() }
            }
            TextButton {
                objectName: "makeAutoEdit"
                visible: !panel.working
                text: panel.mode === "shorts" ? qsTr("Make shorts") : qsTr("Make it")
                variant: "primary"
                enabled: panel.chosen.length > 0
                onClicked: panel.make()
            }
        }
    }
}
