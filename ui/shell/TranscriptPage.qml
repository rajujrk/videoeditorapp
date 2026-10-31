import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import TechieVideoEditor.Theme

// The library's Transcript tab: edit the video by editing what was said. The words, in
// paragraphs at the pauses; click one to go there, shift-click to select through it, then
// Delete (or the button) cuts those words out of the clips. Fillers are marked and can all
// go at once; long pauses between words can be shortened.
ScrollView {
    id: page
    required property var project

    readonly property var words: project.transcript
    property var selected: []  // word indices
    property int anchor: -1
    property real pauseLimit: 0.7
    readonly property int fillerCount: words.filter((w) => w.filler).length
    readonly property var model: project.speechModels.find((m) => m.downloaded)
    // Paragraphs: a new one after a pause of more than 1.5 s.
    readonly property var paragraphs: {
        const list = []
        let last = null
        for (const w of words) {
            if (!last || w.start - last.end > 1.5)
                list.push({ start: w.start, words: [] })
            list[list.length - 1].words.push(w)
            last = w
        }
        return list
    }

    onWordsChanged: {
        selected = []
        anchor = -1
    }
    contentWidth: availableWidth
    clip: true
    focus: true
    Keys.onPressed: (event) => {
        if ((event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace) && selected.length > 0) {
            project.deleteWords(selected)
            event.accepted = true
        }
    }

    function choose(index, modifiers) {
        if ((modifiers & Qt.ShiftModifier) && anchor >= 0) {
            const from = Math.min(anchor, index)
            const to = Math.max(anchor, index)
            const range = []
            for (let i = from; i <= to; ++i)
                range.push(i)
            selected = range
        } else if (modifiers & Qt.ControlModifier) {
            selected = selected.indexOf(index) >= 0 ? selected.filter((i) => i !== index) : selected.concat([index])
            anchor = index
        } else {
            selected = [index]
            anchor = index
        }
        project.seek(words[index].start)
        page.forceActiveFocus()
    }
    function stamp(seconds) {
        const s = Math.floor(seconds)
        return Math.floor(s / 60) + ":" + String(s % 60).padStart(2, "0")
    }

    ColumnLayout {
        width: parent.width - Theme.space.m * 2
        x: Theme.space.m
        spacing: Theme.space.m

        // Nothing yet: transcribe first.
        ColumnLayout {
            visible: page.words.length === 0
            Layout.fillWidth: true
            spacing: Theme.space.s
            Text {
                Layout.fillWidth: true
                text: page.model ? "Transcribe the clips to edit them by their words: delete words here and they are cut from the video."
                                 : "Download a speech model in the Captions tab first; then transcribe the clips here and edit them by their words."
                font: Theme.type.body
                color: Theme.color.textSecondary
                wrapMode: Text.WordWrap
            }
            TextButton {
                objectName: "transcribe"
                visible: page.model !== undefined
                Layout.fillWidth: true
                text: "Transcribe"
                variant: "primary"
                enabled: !page.project.analyzing
                onClicked: page.project.transcribe({ model: page.model.id })
            }
            Text {
                visible: page.project.analyzing
                text: page.project.analysisTask + "… " + Math.round(page.project.analysisProgress * 100) + " %"
                font: Theme.type.body
                color: Theme.color.textPrimary
            }
        }

        // Tools
        ColumnLayout {
            visible: page.words.length > 0
            Layout.fillWidth: true
            spacing: Theme.space.xs
            Flow {
                Layout.fillWidth: true
                spacing: Theme.space.xs
                TextButton {
                    objectName: "deleteWords"
                    text: page.selected.length > 1 ? "Delete " + page.selected.length + " words" : "Delete word"
                    variant: "primary"
                    enabled: page.selected.length > 0
                    tip: "Cuts them out of the clips (⌫)"
                    onClicked: page.project.deleteWords(page.selected)
                }
                TextButton {
                    objectName: "shortenPauses"
                    text: "Shorten pauses"
                    variant: "secondary"
                    tip: "Pauses between words longer than the length below become 0.3 s"
                    onClicked: page.project.removeTranscriptPauses(page.pauseLimit, 0.15)
                }
                TextButton {
                    objectName: "removeFillers"
                    text: "Remove fillers (" + page.fillerCount + ")"
                    variant: "secondary"
                    enabled: page.fillerCount > 0
                    tip: "Cuts every “um”, “uh” and the like"
                    onClicked: page.project.removeFillerWords()
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Text { text: "Pauses longer than"; font: Theme.type.body; color: Theme.color.textSecondary }
                Slider {
                    objectName: "pauseLimit"
                    Layout.fillWidth: true
                    from: 0.3
                    to: 3
                    stepSize: 0.1
                    value: page.pauseLimit
                    onMoved: page.pauseLimit = value
                }
                Text { text: page.pauseLimit.toFixed(1) + " s"; font: Theme.type.mono; color: Theme.color.textPrimary }
            }
        }

        // The words
        Repeater {
            model: page.paragraphs
            delegate: ColumnLayout {
                id: paragraph
                required property var modelData
                Layout.fillWidth: true
                spacing: 2
                Text { text: page.stamp(paragraph.modelData.start); font: Theme.type.mono; color: Theme.color.textSecondary }
                Flow {
                    Layout.fillWidth: true
                    spacing: 2
                    Repeater {
                        model: paragraph.modelData.words
                        delegate: Rectangle {
                            id: word
                            required property var modelData
                            readonly property bool chosen: page.selected.indexOf(modelData.index) >= 0
                            readonly property bool now: page.project.playhead >= modelData.start && page.project.playhead < modelData.end
                            objectName: "word:" + modelData.index
                            implicitWidth: label.implicitWidth + 6
                            implicitHeight: label.implicitHeight + 4
                            radius: 3
                            color: chosen ? Theme.color.accent : now ? Theme.color.accentSubtle : "transparent"
                            Text {
                                id: label
                                anchors.centerIn: parent
                                text: word.modelData.text
                                font.family: Theme.type.body.family
                                font.pixelSize: Theme.type.body.pixelSize
                                font.italic: word.modelData.filler
                                font.underline: word.now && !word.chosen
                                color: word.chosen ? Theme.color.textOnAccent
                                     : word.modelData.filler ? Theme.color.warning : Theme.color.textPrimary
                            }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: (mouse) => page.choose(word.modelData.index, mouse.modifiers)
                            }
                        }
                    }
                }
            }
        }
        Item { implicitHeight: Theme.space.m }
    }
}
