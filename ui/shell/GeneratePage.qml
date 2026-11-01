import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import TechieVideoEditor.Theme

// The library's Generate tab: pictures and stickers (OpenAI), video (Google) and music and
// sound effects (ElevenLabs) from a description, with the person's own keys. What is made
// goes into the media bin with its prompt and maker kept; stickers and sound go straight on
// the timeline at the playhead. Made here lists this project's generated media.
ScrollView {
    id: page
    required property var project
    readonly property var cloud: project.cloud
    signal settingsRequested()

    // "picture", "sticker", "video", "music" or "sfx".
    property string kind: "picture"
    property string shape: "square"
    property string quality: "medium"
    property string videoMode: "text"
    property string aspect: "16:9"
    property string resolution: "720p"
    // Chosen media ids (pictures to work from, in order; the video to extend).
    property var picked: []
    onKindChanged: picked = []
    onVideoModeChanged: picked = []

    readonly property bool running: cloud.job === "image" || cloud.job === "sticker" || cloud.job === "video"
                                    || cloud.job === "music" || cloud.job === "sfx"
    readonly property string provider: kind === "picture" || kind === "sticker" ? "OpenAI"
                                     : kind === "video" ? "Google" : "ElevenLabs"
    readonly property bool hasKey: provider === "OpenAI" ? cloud.hasOpenAiKey
                                 : provider === "Google" ? cloud.hasGoogleKey : cloud.hasElevenLabsKey
    // (Read once: each read of the media list checks every file.)
    readonly property var media: project.media
    readonly property var stills: media.filter((m) => m.still && !m.sticker && !m.missing)
    readonly property var madeVideos: media.filter((m) => m.generated && m.generated.kind === "video" && m.generated.reference)
    readonly property var made: media.filter((m) => m.generated).reverse()
    // How many pictures the choice takes (0: any number up to 16).
    readonly property int wanted: kind === "video" ? (videoMode === "image" ? 1 : videoMode === "frames" ? 2 : videoMode === "extend" ? 1 : 0)
                                                    : 0
    readonly property bool ready: hasKey && !running && cloud.job === ""
                                  && (prompt.text.trim() !== "" || (kind === "video" && (videoMode === "extend" || videoMode === "image" || videoMode === "frames")))
                                  && (wanted === 0 || picked.length === wanted)

    function toggle(id) {
        const at = picked.indexOf(id)
        const next = picked.slice()
        if (at >= 0)
            next.splice(at, 1)
        else if (wanted === 1)
            next.splice(0, next.length, id)
        else if (wanted === 0 ? next.length < 16 : next.length < wanted)
            next.push(id)
        picked = next
    }
    function generate() {
        const text = prompt.text
        if (kind === "picture" || kind === "sticker")
            project.generateImage(text, { size: shape, quality: quality, inputs: picked, sticker: kind === "sticker" })
        else if (kind === "video")
            project.generateVideo(text, videoMode === "extend" ? { mode: "extend", extend: picked[0], aspect: aspect, resolution: resolution }
                                                               : { mode: videoMode, images: picked, aspect: aspect, resolution: resolution })
        else
            project.generateSound(text, { kind: kind, seconds: length.value, instrumental: instrumental.checked, loop: loop.checked })
    }
    function clock(seconds) { return Math.floor(seconds / 60) + ":" + String(seconds % 60).padStart(2, "0") }

    component Chip: AbstractButton {
        id: chip
        property string label
        property string badge
        hoverEnabled: true
        leftPadding: chip.badge !== "" ? Theme.space.xs : Theme.space.m
        rightPadding: Theme.space.m
        implicitHeight: 26
        background: Rectangle {
            radius: height / 2
            color: chip.checked ? Theme.color.accentSubtle : chip.hovered ? Theme.color.surfaceHover : "transparent"
            border.color: chip.checked ? Theme.color.accent : Theme.color.borderSubtle
        }
        contentItem: Row {
            spacing: Theme.space.xxs
            Rectangle {
                visible: chip.badge !== ""
                anchors.verticalCenter: parent.verticalCenter
                width: 16
                height: 16
                radius: 8
                color: Theme.color.accent
                Text { anchors.centerIn: parent; text: chip.badge; font: Theme.type.caption; color: "#FFFFFF" }
            }
            Text {
                id: chipText
                anchors.verticalCenter: parent.verticalCenter
                text: chip.label
                font: Theme.type.caption
                color: chip.checked ? Theme.color.accentText : Theme.color.textSecondary
            }
        }
    }

    contentWidth: availableWidth
    clip: true

    ColumnLayout {
        width: page.availableWidth - Theme.space.m * 2
        x: Theme.space.m
        spacing: Theme.space.s

        // What to make
        Flow {
            Layout.fillWidth: true
            spacing: Theme.space.xs
            Repeater {
                model: [{ id: "picture", label: "Picture" }, { id: "sticker", label: "Sticker" }, { id: "video", label: "Video" },
                        { id: "music", label: "Music" }, { id: "sfx", label: "Sound effect" }]
                delegate: Chip {
                    required property var modelData
                    objectName: "generateKind:" + modelData.id
                    label: modelData.label
                    checked: page.kind === modelData.id
                    enabled: !page.running
                    onClicked: page.kind = modelData.id
                }
            }
        }

        // No key yet
        ColumnLayout {
            visible: !page.hasKey
            Layout.fillWidth: true
            Text {
                Layout.fillWidth: true
                text: "Add your " + (page.provider === "Google" ? "Google AI (Gemini API)" : page.provider) + " key in AI settings to make "
                      + (page.kind === "video" ? "video." : page.kind === "music" || page.kind === "sfx" ? "sound." : "pictures.")
                font: Theme.type.body
                color: Theme.color.textSecondary
                wrapMode: Text.WordWrap
            }
            TextButton {
                objectName: "openAiSettings"
                text: "AI settings…"
                iconName: "assistant"
                variant: "secondary"
                onClicked: page.settingsRequested()
            }
        }

        // Video: how to start
        Flow {
            visible: page.kind === "video"
            Layout.fillWidth: true
            spacing: Theme.space.xs
            Repeater {
                model: [{ id: "text", label: "From words" }, { id: "image", label: "From a picture" },
                        { id: "frames", label: "First and last picture" }, { id: "extend", label: "Extend a video" }]
                delegate: Chip {
                    required property var modelData
                    objectName: "videoMode:" + modelData.id
                    label: modelData.label
                    checked: page.videoMode === modelData.id
                    enabled: !page.running
                    onClicked: page.videoMode = modelData.id
                }
            }
        }

        TextArea {
            id: prompt
            objectName: "generatePrompt"
            Layout.fillWidth: true
            Layout.preferredHeight: 96
            wrapMode: TextEdit.Wrap
            enabled: !page.running
            font: Theme.type.body
            color: Theme.color.textPrimary
            placeholderText: page.kind === "sticker" ? "A sticker of… (one object, it is cut out)"
                           : page.kind === "video" ? (page.videoMode === "extend" ? "What happens next (optional)" : "Describe the shot: what, where, how the camera moves")
                           : page.kind === "music" ? "The mood, genre, instruments and tempo (no artist or song names)"
                           : page.kind === "sfx" ? "The sound: what makes it, where, how long"
                           : "Describe the picture"
            background: Rectangle { radius: Theme.radius.m; color: Theme.color.surfaceSunken; border.color: Theme.color.borderSubtle }
        }

        // Pictures and stickers: shape and quality
        Flow {
            visible: page.kind === "picture" || page.kind === "sticker"
            Layout.fillWidth: true
            spacing: Theme.space.xs
            Repeater {
                model: [{ id: "square", label: "Square" }, { id: "landscape", label: "Landscape" }, { id: "portrait", label: "Portrait" }]
                delegate: Chip {
                    required property var modelData
                    objectName: "imageShape:" + modelData.id
                    visible: page.kind === "picture" || modelData.id === "square"
                    label: modelData.label
                    checked: page.shape === modelData.id || (page.kind === "sticker" && modelData.id === "square")
                    onClicked: page.shape = modelData.id
                }
            }
            Repeater {
                model: [{ id: "low", label: "Draft" }, { id: "medium", label: "Good" }, { id: "high", label: "Best" }]
                delegate: Chip {
                    required property var modelData
                    objectName: "imageQuality:" + modelData.id
                    label: modelData.label
                    checked: page.quality === modelData.id
                    onClicked: page.quality = modelData.id
                }
            }
        }

        // Video: shape
        Flow {
            visible: page.kind === "video"
            Layout.fillWidth: true
            spacing: Theme.space.xs
            Repeater {
                model: ["16:9", "9:16"]
                delegate: Chip {
                    required property string modelData
                    objectName: "videoAspect:" + modelData
                    label: modelData === "16:9" ? "Wide 16:9" : "Tall 9:16"
                    checked: page.aspect === modelData
                    onClicked: page.aspect = modelData
                }
            }
            Repeater {
                model: ["720p", "1080p"]
                delegate: Chip {
                    required property string modelData
                    objectName: "videoResolution:" + modelData
                    label: modelData
                    checked: page.resolution === modelData
                    onClicked: page.resolution = modelData
                }
            }
        }

        // Pictures to work from (or the video to extend)
        Text {
            visible: pickFlow.visible
            Layout.fillWidth: true
            text: page.kind === "video" && page.videoMode === "extend" ? "The video to extend"
                : page.kind === "video" && page.videoMode === "frames" ? "The first picture, then the last"
                : page.kind === "video" ? "The picture to start from"
                : "Work from pictures in the media bin (optional)"
            font: Theme.type.label
            color: Theme.color.textSecondary
        }
        Flow {
            id: pickFlow
            visible: (page.kind === "picture" || page.kind === "sticker" || (page.kind === "video" && page.videoMode !== "text"))
            Layout.fillWidth: true
            spacing: Theme.space.xs
            Repeater {
                model: page.kind === "video" && page.videoMode === "extend" ? page.madeVideos : page.stills
                delegate: Chip {
                    required property var modelData
                    objectName: "pickMedia:" + modelData.name
                    label: modelData.name
                    checked: page.picked.indexOf(modelData.id) >= 0
                    badge: page.wanted === 2 && checked ? String(page.picked.indexOf(modelData.id) + 1) : ""
                    enabled: !page.running
                    onClicked: page.toggle(modelData.id)
                }
            }
            Text {
                visible: (page.kind === "video" && page.videoMode === "extend" ? page.madeVideos : page.stills).length === 0
                text: page.kind === "video" && page.videoMode === "extend" ? "No videos made here yet." : "No pictures in the media bin."
                font: Theme.type.caption
                color: Theme.color.textDisabled
            }
        }

        // Music and sound: length and switches
        RowLayout {
            visible: page.kind === "music" || page.kind === "sfx"
            Layout.fillWidth: true
            Text { text: "Length"; font: Theme.type.label; color: Theme.color.textSecondary }
            Slider {
                id: length
                objectName: "soundLength"
                Layout.fillWidth: true
                from: page.kind === "music" ? 5 : 0.5
                to: page.kind === "music" ? 180 : 30
                stepSize: page.kind === "music" ? 5 : 0.5
                value: page.kind === "music" ? 30 : 3
            }
            Text { text: page.kind === "music" ? page.clock(Math.round(length.value)) : length.value.toFixed(1) + " s"; font: Theme.type.label; color: Theme.color.textPrimary }
        }
        ToggleSwitch { id: instrumental; objectName: "musicInstrumental"; visible: page.kind === "music"; text: "Instrumental"; checked: true }
        ToggleSwitch { id: loop; objectName: "soundLoop"; visible: page.kind === "sfx"; text: "Loops smoothly"; checked: false }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.space.s
            TextButton {
                objectName: "generateButton"
                Layout.fillWidth: true
                visible: !page.running
                text: page.kind === "sticker" ? "Make the sticker" : page.kind === "video" && page.videoMode === "extend" ? "Extend" : "Generate"
                iconName: "generate"
                variant: "primary"
                enabled: page.ready
                onClicked: page.generate()
            }
            TextButton {
                objectName: "stopGenerating"
                Layout.fillWidth: true
                visible: page.running
                text: "Stop"
                variant: "secondary"
                onClicked: page.cloud.cancel()
            }
        }
        Text {
            objectName: "generateStatus"
            visible: page.running
            Layout.fillWidth: true
            text: page.cloud.status + " " + page.clock(page.cloud.elapsed)
            font: Theme.type.caption
            color: Theme.color.textSecondary
            wrapMode: Text.WordWrap
        }
        // Whose terms apply to what is made.
        Text {
            objectName: "generateTerms"
            Layout.fillWidth: true
            text: page.provider === "OpenAI" ? "Made by OpenAI with your key; under OpenAI's terms the pictures are yours to use."
                : page.provider === "Google" ? "Made by Google's Gemini Omni Flash with your key. Videos carry Google's invisible SynthID watermark."
                : page.kind === "music" ? "Made by ElevenLabs with your key. Paid plans allow commercial use except film, TV, radio and studio games (Enterprise Music only); don't name artists, songs or lyrics."
                : "Made by ElevenLabs with your key; how you may use it depends on your ElevenLabs plan."
            font: Theme.type.caption
            color: Theme.color.textSecondary
            wrapMode: Text.WordWrap
        }

        // Made here
        Divider { Layout.fillWidth: true; implicitHeight: 1; visible: page.made.length > 0 }
        Text { visible: page.made.length > 0; text: "Made here"; font: Theme.type.heading; color: Theme.color.textPrimary }
        Repeater {
            model: page.made
            delegate: Rectangle {
                id: madeRow
                required property var modelData
                objectName: "made:" + modelData.name
                Layout.fillWidth: true
                implicitHeight: madeColumn.implicitHeight + Theme.space.s * 2
                radius: Theme.radius.m
                color: Theme.color.surfaceRaised
                border.color: Theme.color.borderSubtle
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.space.s
                    spacing: Theme.space.s
                    Rectangle {
                        Layout.preferredWidth: 56
                        Layout.preferredHeight: 40
                        radius: Theme.radius.s
                        color: Theme.color.surfaceSunken
                        Icon {
                            anchors.centerIn: parent
                            name: madeRow.modelData.hasVideo ? "media" : "audio"
                            color: Theme.color.textDisabled
                        }
                        Image {
                            anchors.fill: parent
                            anchors.margins: 1
                            visible: madeRow.modelData.hasVideo
                            asynchronous: true
                            fillMode: Image.PreserveAspectFit
                            source: madeRow.modelData.hasVideo ? "image://tvethumb/" + encodeURIComponent(madeRow.modelData.path) + "@"
                                                                 + (madeRow.modelData.duration * 0.3).toFixed(2) + "@80" : ""
                        }
                    }
                    ColumnLayout {
                        id: madeColumn
                        Layout.fillWidth: true
                        spacing: 2
                        Text {
                            Layout.fillWidth: true
                            text: madeRow.modelData.generated.prompt || madeRow.modelData.name
                            font: Theme.type.label
                            color: Theme.color.textPrimary
                            elide: Text.ElideRight
                            maximumLineCount: 2
                            wrapMode: Text.WordWrap
                        }
                        Text {
                            Layout.fillWidth: true
                            text: madeRow.modelData.name + " · " + madeRow.modelData.generated.model
                                  + (madeRow.modelData.generated.source ? " · from " + madeRow.modelData.generated.source : "")
                            font: Theme.type.caption
                            color: Theme.color.textSecondary
                            elide: Text.ElideRight
                        }
                    }
                    TextButton {
                        objectName: "addMade:" + madeRow.modelData.name
                        visible: madeRow.modelData.hasVideo
                        text: "Add"
                        variant: "ghost"
                        tip: "Add to the end of the main track"
                        onClicked: page.project.addClip(madeRow.modelData.id)
                    }
                    TextButton {
                        objectName: "extendMade:" + madeRow.modelData.name
                        visible: madeRow.modelData.generated.kind === "video" && madeRow.modelData.generated.reference !== ""
                        text: "Extend"
                        variant: "ghost"
                        enabled: !page.running
                        onClicked: { page.kind = "video"; page.videoMode = "extend"; page.picked = [madeRow.modelData.id] }
                    }
                }
            }
        }
        Item { implicitHeight: Theme.space.m }
    }
}
