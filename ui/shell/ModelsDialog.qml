import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import TechieVideoEditor.Theme
import TechieVideoEditor.Player

// The models that let features run on this Mac, to get in one go: offered once when the app
// is first opened, and from the Home screen's "On-device models" any time after. Each comes
// from its makers and is checked (size and SHA-256) before it is used; the ones not chosen
// here are offered again where they are needed. One downloads at a time, in the order shown;
// closing the dialog lets them carry on.
Popup {
    id: dialog
    enter: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.motion.normal; easing.type: Easing.OutCubic }
            NumberAnimation { property: "scale"; from: 0.96; to: 1; duration: Theme.motion.normal; easing.type: Easing.OutCubic }
        }
    }
    required property var project

    // The models, with what each is for. Upscaling's model comes from Apple through macOS
    // (shown only on Macs that can upscale).
    readonly property var rows: {
        const speech = project.speechModels
        const info = (id) => id === "deepfilternet3" ? project.enhanceModel
                           : id === "depth-anything-v2-small-f16" ? project.depthModel
                           : speech.find((m) => m.id === id) || ({})
        const english = Language.current === "" || Language.current.indexOf("en") === 0
        const list = [
            { id: "base.en-q5_1", title: qsTr("Speech to text · English"), recommended: english,
              use: qsTr("Captions, the transcript, editing by words and smart search") },
            { id: "base-q5_1", title: qsTr("Speech to text · 99 languages"), recommended: !english,
              use: qsTr("The same, for speech in other languages") },
            { id: "deepfilternet3", title: qsTr("Speech cleanup"), recommended: true,
              use: qsTr("Enhance speech: noise, hum and room echo taken out of voices") },
            { id: "depth-anything-v2-small-f16", title: qsTr("Depth"), recommended: true,
              use: qsTr("Depth of field: what is nearer or farther than the focus blurred") }
        ].map((row) => {
            const m = info(row.id)
            return Object.assign(row, { size: m.size || 0, downloaded: m.downloaded === true })
        })
        const upscale = project.upscaleModel.status
        if (upscale !== "unsupported")
            list.push({ id: "upscale", title: qsTr("Upscaling"), recommended: false, size: 0,
                        use: qsTr("Upscale: sharper pictures at 2× or 4× (from Apple, through macOS)"),
                        downloaded: upscale === "ready" })
        return list
    }
    // id → chosen (set when the dialog opens: what is recommended and not here yet).
    property var chosen: ({})
    // Still to download after the one downloading.
    property var queue: []
    // id → why it failed.
    property var errors: ({})
    readonly property string downloading: project.modelDownload.id || ""
    readonly property bool upscaling: project.upscaleModel.status === "downloading"
    readonly property bool busy: downloading !== "" || queue.length > 0 || upscaling
    readonly property real chosenBytes: rows.reduce((sum, r) => sum + (chosen[r.id] && !r.downloaded ? r.size : 0), 0)
    readonly property int chosenCount: rows.filter((r) => chosen[r.id] && !r.downloaded).length
    readonly property bool allHere: rows.every((r) => r.downloaded || (r.id === "upscale"))

    modal: true
    focus: true
    anchors.centerIn: Overlay.overlay
    width: 560
    padding: Theme.space.l
    onAboutToShow: {
        if (busy)
            return
        const picked = {}
        for (const row of rows)
            picked[row.id] = row.recommended && !row.downloaded
        chosen = picked
        errors = {}
    }

    function choose(id, on) {
        const picked = Object.assign({}, chosen)
        picked[id] = on
        chosen = picked
    }
    function start() {
        errors = {}
        const ids = rows.filter((r) => chosen[r.id] && !r.downloaded).map((r) => r.id)
        if (ids.indexOf("upscale") >= 0 && !project.downloadUpscaleModel())
            fail("upscale", project.lastError)
        queue = ids.filter((id) => id !== "upscale")
        next()
    }
    function next() {
        while (queue.length > 0) {
            const rest = queue.slice()
            const id = rest.shift()
            queue = rest
            if (project.downloadSpeechModel(id))
                return
            fail(id, project.lastError)
        }
    }
    function stop() {
        queue = []
        if (downloading !== "")
            project.cancelModelDownload()
    }
    function fail(id, message) {
        const e = Object.assign({}, errors)
        e[id] = message
        errors = e
    }

    Connections {
        target: dialog.project
        function onModelDownloadFinished(modelId, ok, error) {
            if (!ok && error !== "")
                dialog.fail(modelId, error)
            if (!ok && error === "") // cancelled: the rest too
                dialog.queue = []
            dialog.next()
        }
    }

    Overlay.modal: Rectangle { color: Qt.alpha("#000000", Theme.dark ? 0.55 : 0.35) }
    background: Rectangle {
        radius: Theme.radius.l
        color: Theme.color.surfaceRaised
        border.color: Theme.color.borderStrong
    }

    contentItem: ColumnLayout {
        spacing: Theme.space.m

        Text {
            objectName: "modelsTitle"
            text: qsTr("Models for working on this Mac")
            font: Theme.type.title
            color: Theme.color.textPrimary
        }
        Text {
            Layout.fillWidth: true
            text: qsTr("These features run on this Mac, so your media never leaves it. Each needs a model, downloaded once from its makers and checked before it is used. Get them now, or later: each is offered again where it is needed.")
            font: Theme.type.body
            color: Theme.color.textSecondary
            wrapMode: Text.WordWrap
        }

        Repeater {
            model: dialog.rows
            delegate: Rectangle {
                id: row
                required property var modelData
                readonly property bool active: dialog.downloading === modelData.id
                                               || (modelData.id === "upscale" && dialog.upscaling)
                readonly property bool waiting: dialog.queue.indexOf(modelData.id) >= 0
                readonly property string error: dialog.errors[modelData.id] || ""
                objectName: "modelRow:" + modelData.id
                Layout.fillWidth: true
                implicitHeight: rowLayout.implicitHeight + Theme.space.s * 2
                radius: Theme.radius.m
                color: Theme.color.surface
                border.color: row.active ? Theme.color.accent : Theme.color.borderSubtle

                RowLayout {
                    id: rowLayout
                    anchors.fill: parent
                    anchors.margins: Theme.space.s
                    anchors.leftMargin: Theme.space.m
                    spacing: Theme.space.m

                    ToggleSwitch {
                        objectName: "modelChoice:" + row.modelData.id
                        visible: !row.modelData.downloaded && !row.active && !row.waiting
                        enabled: !dialog.busy
                        checked: dialog.chosen[row.modelData.id] === true
                        onToggled: dialog.choose(row.modelData.id, checked)
                    }
                    Text {
                        visible: row.modelData.downloaded
                        Layout.preferredWidth: 32
                        horizontalAlignment: Text.AlignHCenter
                        text: "✓"
                        font: Theme.type.heading
                        color: Theme.color.success
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Text {
                            Layout.fillWidth: true
                            text: row.modelData.title
                            font: Theme.type.label
                            color: Theme.color.textPrimary
                            elide: Text.ElideRight
                        }
                        Text {
                            Layout.fillWidth: true
                            text: row.modelData.use
                            font: Theme.type.caption
                            color: Theme.color.textSecondary
                            wrapMode: Text.WordWrap
                        }
                        ProgressBar {
                            id: bar
                            visible: row.active
                            Layout.fillWidth: true
                            Layout.topMargin: Theme.space.xs
                            value: row.modelData.id === "upscale" ? dialog.project.upscaleModel.progress
                                                                  : (dialog.project.modelDownload.progress || 0)
                            background: Rectangle { implicitHeight: 4; radius: 2; color: Theme.color.borderSubtle }
                            contentItem: Item {
                                implicitHeight: 4
                                Rectangle { width: parent.width * bar.visualPosition; height: parent.height; radius: 2; color: Theme.color.accent }
                            }
                        }
                        Text {
                            objectName: "modelError:" + row.modelData.id
                            visible: row.error !== ""
                            Layout.fillWidth: true
                            text: row.error
                            font: Theme.type.caption
                            color: Theme.color.warning
                            wrapMode: Text.WordWrap
                        }
                    }
                    Text {
                        objectName: "modelState:" + row.modelData.id
                        text: row.modelData.downloaded ? qsTr("On this Mac")
                            : row.active ? (row.modelData.id === "upscale" ? Fmt.percent(dialog.project.upscaleModel.progress)
                                                                          : Fmt.percent(dialog.project.modelDownload.progress || 0))
                            : row.waiting ? qsTr("Waiting")
                            : row.modelData.size > 0 ? Fmt.mb(row.modelData.size) : qsTr("From macOS")
                        font: Theme.type.caption
                        color: row.modelData.downloaded ? Theme.color.success : Theme.color.textSecondary
                    }
                }
            }
        }

        Text {
            Layout.fillWidth: true
            text: qsTr("More accurate speech models (Small and Large, 190–574 MB) are in the Captions tab.")
            font: Theme.type.caption
            color: Theme.color.textSecondary
            wrapMode: Text.WordWrap
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.space.s
            Text {
                objectName: "modelsSummary"
                Layout.fillWidth: true
                text: dialog.busy ? qsTr("Downloading… you can close this and keep working.")
                    : dialog.allHere ? qsTr("Every model is on this Mac.")
                    : dialog.chosenCount > 0 ? qsTr("%1 to download").arg(Fmt.mb(dialog.chosenBytes))
                    : qsTr("Nothing chosen")
                font: Theme.type.body
                color: Theme.color.textSecondary
                elide: Text.ElideRight
            }
            TextButton {
                objectName: "modelsStop"
                visible: dialog.busy && !dialog.upscaling
                text: qsTr("Stop")
                variant: "ghost"
                onClicked: dialog.stop()
            }
            TextButton {
                objectName: "modelsClose"
                text: dialog.busy || dialog.allHere ? qsTr("Close") : qsTr("Later")
                variant: "secondary"
                onClicked: dialog.close()
            }
            TextButton {
                objectName: "modelsDownload"
                visible: !dialog.allHere
                enabled: !dialog.busy && dialog.chosenCount > 0
                text: qsTr("Download")
                iconName: "import"
                variant: "primary"
                onClicked: dialog.start()
            }
        }
    }
}
