import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import TechieVideoEditor.Theme

// The Media tab's smart search: the moments of the media bin matching the search — in what
// the pictures show (labels Vision gives them on this Mac), what is said (media with a
// transcript) and the text on screen — and, while searching, how much of the bin it can
// reach, with a button to look at the rest. A moment opens in the source monitor with In and Out around it;
// double-clicked, it goes on the timeline at the playhead.
ColumnLayout {
    id: search
    required property var project
    property string query: ""
    signal momentRequested(var media, real start, real end)

    readonly property string trimmed: query.trim()
    readonly property var status: project.searchStatus
    // (Started here: the analysis running is this one.)
    property bool started: false
    readonly property bool looking: started && project.analyzing
    // The moments found ([] when not searching), and the media they are in.
    property var hits: []
    readonly property var hitIds: hits.map((h) => h.mediaId)

    function refresh() { hits = trimmed === "" ? [] : project.searchMedia(trimmed) }
    function clock(seconds) {
        const s = Math.floor(seconds)
        return Math.floor(s / 60) + ":" + String(s % 60).padStart(2, "0")
    }

    onTrimmedChanged: searchDelay.restart()
    Timer { id: searchDelay; interval: 200; onTriggered: search.refresh() }
    Connections {
        target: search.project
        function onSearchIndexChanged() { if (search.trimmed !== "") searchDelay.restart() }
        function onAnalysisFinished() { search.started = false }
    }

    spacing: Theme.space.xs

    // How far search reaches, and looking at the rest.
    RowLayout {
        objectName: "searchReach"
        // (Only while searching or looking: the bin keeps its room otherwise.)
        visible: (search.trimmed !== "" || search.looking) && (search.status.lookable > 0 || search.status.transcribed > 0)
        Layout.fillWidth: true
        Layout.leftMargin: Theme.space.m
        Layout.rightMargin: Theme.space.m
        spacing: Theme.space.s
        Text {
            objectName: "searchReachText"
            Layout.fillWidth: true
            text: search.looking
                  ? qsTr("Looking at the media… %1 %").arg(Math.round(search.project.analysisProgress * 100))
                  : (search.status.lookable === 0 ? ""
                     : search.status.looked < search.status.lookable
                     ? qsTr("Search finds what is in %1 of %2 pictures and videos.").arg(search.status.looked).arg(search.status.lookable)
                     : qsTr("Search finds what is in every picture and video."))
                    + " "
                    + (search.status.transcribed > 0
                       ? (search.status.transcribed === 1 ? qsTr("It hears what is said in 1 transcribed file.")
                                                           : qsTr("It hears what is said in %1 transcribed files.").arg(search.status.transcribed))
                       : qsTr("Transcribe clips (Transcript tab) to search what is said too."))
            font: Theme.type.caption
            color: Theme.color.textSecondary
            wrapMode: Text.WordWrap
        }
        TextButton {
            objectName: "lookAtMedia"
            visible: !search.looking && search.status.looked < search.status.lookable
            text: qsTr("Look at media")
            iconName: "search"
            variant: "secondary"
            enabled: !search.project.analyzing
            tip: qsTr("Labels what is in each picture and video, and reads the text on screen, on this Mac")
            onClicked: search.started = search.project.analyzeForSearch() && search.project.analyzing
        }
        TextButton {
            objectName: "stopLooking"
            visible: search.looking
            text: qsTr("Stop")
            variant: "ghost"
            onClicked: search.project.cancelAnalysis()
        }
    }

    // The moments found.
    Text {
        objectName: "momentsTitle"
        visible: search.trimmed !== ""
        Layout.fillWidth: true
        Layout.leftMargin: Theme.space.m
        Layout.rightMargin: Theme.space.m
        text: search.hits.length === 0
              ? (search.status.looked === 0 && search.status.transcribed === 0
                 ? qsTr("Nothing found for “%1” in the names. Look at the media to search what is in them too.").arg(search.trimmed)
                 : qsTr("No moments found for “%1”.").arg(search.trimmed))
              : search.hits.length === 1 ? qsTr("1 moment") : qsTr("%1 moments").arg(search.hits.length)
        font: search.hits.length === 0 ? Theme.type.caption : Theme.type.label
        color: search.hits.length === 0 ? Theme.color.textSecondary : Theme.color.textPrimary
        wrapMode: Text.WordWrap
    }

    ListView {
        id: moments
        objectName: "searchMoments"
        visible: search.hits.length > 0
        Layout.fillWidth: true
        Layout.preferredHeight: Math.min(contentHeight, 64 * 4 + Theme.space.xs * 3)
        Layout.leftMargin: Theme.space.m
        Layout.rightMargin: Theme.space.m
        clip: true
        spacing: Theme.space.xs
        boundsBehavior: Flickable.StopAtBounds
        model: search.hits
        ScrollIndicator.vertical: ScrollIndicator {}

        delegate: Rectangle {
            id: moment
            required property var modelData
            required property int index
            objectName: "moment:" + index
            width: moments.width
            height: 64
            radius: Theme.radius.m
            color: momentMouse.containsMouse ? Theme.color.surfaceHover : Theme.color.surfaceRaised
            border.color: momentMouse.containsMouse ? Theme.color.borderStrong : Theme.color.borderSubtle

            RowLayout {
                anchors.fill: parent
                anchors.margins: Theme.space.xs
                spacing: Theme.space.s

                // The moment's picture.
                Rectangle {
                    Layout.preferredWidth: 80
                    Layout.fillHeight: true
                    radius: Theme.radius.s
                    color: Theme.color.surfaceSunken
                    clip: true
                    Icon { anchors.centerIn: parent; name: "media"; color: Theme.color.textDisabled }
                    Image {
                        anchors.fill: parent
                        asynchronous: true
                        fillMode: Image.PreserveAspectCrop
                        source: "image://tvethumb/!" + encodeURIComponent(moment.modelData.path) + "@"
                                + ((moment.modelData.start + moment.modelData.end) / 2).toFixed(2) + "@" + Math.round(height * 2)
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.space.xs
                        Text {
                            Layout.fillWidth: true
                            text: moment.modelData.name
                            font: Theme.type.label
                            color: Theme.color.textPrimary
                            elide: Text.ElideMiddle
                        }
                        Text {
                            objectName: "momentTime:" + moment.index
                            visible: !moment.modelData.still
                            text: search.clock(moment.modelData.start) + "–" + search.clock(moment.modelData.end)
                            font: Theme.type.caption
                            color: Theme.color.textSecondary
                        }
                    }
                    // How it was found.
                    Row {
                        spacing: Theme.space.xxs
                        Repeater {
                            model: [{ on: moment.modelData.seen, label: qsTr("Seen") },
                                    { on: moment.modelData.said, label: qsTr("Said") },
                                    { on: moment.modelData.shown, label: qsTr("On screen") }].filter((k) => k.on)
                            delegate: Rectangle {
                                required property var modelData
                                width: kindLabel.implicitWidth + Theme.space.s
                                height: kindLabel.implicitHeight + 2
                                radius: Theme.radius.s
                                color: Theme.color.accentSubtle
                                Text {
                                    id: kindLabel
                                    anchors.centerIn: parent
                                    text: parent.modelData.label
                                    font: Theme.type.caption
                                    color: Theme.color.accentText
                                }
                            }
                        }
                    }
                    Text {
                        objectName: "momentSnippet:" + moment.index
                        Layout.fillWidth: true
                        text: moment.modelData.snippet
                        font: Theme.type.caption
                        color: Theme.color.textSecondary
                        elide: Text.ElideRight
                    }
                }
            }

            MouseArea {
                id: momentMouse
                anchors.fill: parent
                hoverEnabled: true
                onClicked: {
                    const media = search.project.media.find((m) => m.id === moment.modelData.mediaId)
                    if (media)
                        search.momentRequested(media, moment.modelData.start, moment.modelData.end)
                }
                onDoubleClicked: moment.modelData.still
                                 ? search.project.addClip(moment.modelData.mediaId)
                                 : search.project.placeMediaRange(moment.modelData.mediaId, moment.modelData.start,
                                                                  moment.modelData.end, "insert")
            }
            ToolTip {
                visible: momentMouse.containsMouse
                delay: 800
                text: qsTr("Click to open in the source monitor with In and Out around it; double-click to put it on the timeline at the playhead")
            }
        }
    }
}
