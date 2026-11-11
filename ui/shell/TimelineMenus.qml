import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import TechieVideoEditor.Theme
import TechieVideoEditor.Player

// The timeline's right-click menus: on a clip, on a track's header, and on empty lane space,
// with the track rename box and the questions they ask first.
Item {
    id: menus
    required property var project
    property var timeline: null // (its track height and blade)

    // A clip's menu acts on the selection when the clip is part of it (else on the clip).
    function openClipMenu(clip, track, item, x, y) {
        if (project.selectedClips.indexOf(clip.id) < 0)
            project.selectedClip = clip.id
        clipMenu.forClip = clip
        clipMenu.track = track
        clipMenu.popup(item, x, y)
    }
    function openTrackMenu(track, item, x, y) {
        trackMenu.track = track
        trackMenu.popup(item, x, y)
    }
    function openTransitionMenu(transition, item, x, y) {
        transitionMenu.transition = transition
        transitionMenu.popup(item, x, y)
    }
    function openPasteAttributes() { pasteBox.open() }
    function openLaneMenu(track, seconds, item, x, y) {
        laneMenu.track = track
        laneMenu.seconds = seconds
        laneMenu.popup(item, x, y)
    }
    // A new track of a kind: pictures above the given track (or on top), sound below the
    // given sound track (or at the very bottom).
    function addTrack(kind, beside) {
        if (kind === "audio") {
            const tracks = project.tracks // top to bottom
            const below = beside && beside.kind === "audio" ? beside : tracks[tracks.length - 1]
            return project.addTrack("audio", below ? below.id : -1, false)
        }
        return project.addTrack(kind, beside && beside.kind !== "audio" ? beside.id : -1, true)
    }
    function deleteTrack(track) {
        if (track.clips.length === 0) {
            project.removeTrack(track.id)
            return
        }
        confirm.ask(qsTr("Delete \"%1\"?").arg(track.name),
                    track.clips.length === 1 ? qsTr("Its clip goes with it. You can undo this.")
                                             : qsTr("Its %1 clips go with it. You can undo this.").arg(Fmt.num(track.clips.length)),
                    qsTr("Delete track"), () => project.removeTrack(track.id))
    }

    readonly property bool playheadInClip: clipMenu.forClip !== null
                                           && project.playhead > clipMenu.forClip.start
                                           && project.playhead < clipMenu.forClip.start + clipMenu.forClip.duration
    readonly property bool hasSpeed: clipMenu.forClip !== null && clipMenu.forClip.isTitle !== true && clipMenu.forClip.isAdjustment !== true
                                     && clipMenu.forClip.isDrawing !== true && clipMenu.forClip.isStill !== true

    AppMenu {
        id: clipMenu
        objectName: "clipMenu"
        property var forClip: null
        property var track: null

        AppMenuItem { objectName: "clipMenu:cut"; text: qsTr("Cut"); shortcut: KeyMap.text("cut"); onTriggered: menus.project.cutClips() }
        AppMenuItem { objectName: "clipMenu:copy"; text: qsTr("Copy"); shortcut: KeyMap.text("copy"); onTriggered: menus.project.copyClips() }
        AppMenuItem {
            objectName: "clipMenu:paste"
            text: qsTr("Paste at the playhead"); shortcut: KeyMap.text("paste")
            enabled: menus.project.canPaste
            onTriggered: menus.project.pasteClips(clipMenu.track ? clipMenu.track.id : -1)
        }
        AppMenuItem { objectName: "clipMenu:duplicate"; text: qsTr("Duplicate"); shortcut: KeyMap.text("duplicate"); onTriggered: menus.project.duplicateClips() }
        AppMenuItem {
            objectName: "clipMenu:copyAttributes"
            text: qsTr("Copy attributes")
            onTriggered: menus.project.copyAttributes(clipMenu.forClip.id)
        }
        AppMenuItem {
            objectName: "clipMenu:pasteAttributes"
            text: qsTr("Paste attributes…"); shortcut: KeyMap.text("pasteAttributes")
            enabled: menus.project.hasCopiedAttributes
            onTriggered: pasteBox.open()
        }
        AppMenuItem { objectName: "clipMenu:delete"; text: qsTr("Delete"); shortcut: KeyMap.text("delete"); onTriggered: menus.project.deleteClip(clipMenu.forClip.id) }
        AppMenuItem {
            objectName: "clipMenu:rippleDelete"
            text: qsTr("Ripple delete (close the gap)"); shortcut: KeyMap.text("rippleDelete")
            onTriggered: menus.project.rippleDeleteClip(clipMenu.forClip.id)
        }
        AppMenuSeparator {}
        AppMenuItem {
            objectName: "clipMenu:split"
            text: qsTr("Split at the playhead"); shortcut: KeyMap.text("split")
            enabled: menus.playheadInClip
            onTriggered: menus.project.splitAtPlayhead()
        }
        AppMenuItem {
            objectName: "clipMenu:trimStart"
            text: qsTr("Trim the start to the playhead")
            enabled: menus.playheadInClip
            onTriggered: menus.project.trimClip(clipMenu.forClip.id, true, menus.project.playhead)
        }
        AppMenuItem {
            objectName: "clipMenu:trimEnd"
            text: qsTr("Trim the end to the playhead")
            enabled: menus.playheadInClip
            onTriggered: menus.project.trimClip(clipMenu.forClip.id, false, menus.project.playhead)
        }
        AppMenuSeparator {}
        AppMenuItem {
            objectName: "clipMenu:detachAudio"
            text: qsTr("Detach audio")
            visible: clipMenu.forClip !== null && clipMenu.track !== null && clipMenu.track.kind === "video"
                     && clipMenu.forClip.hasAudio === true && clipMenu.forClip.audioEnabled === true && clipMenu.forClip.isCompound !== true
            onTriggered: menus.project.detachAudio(clipMenu.forClip.id)
        }
        AppMenuItem {
            objectName: "clipMenu:crossfade"
            text: qsTr("Crossfade from the clip before")
            visible: clipMenu.track !== null && clipMenu.track.kind === "audio"
            onTriggered: menus.project.addTransition("tve.transition.dissolve", clipMenu.forClip.id)
        }
        AppMenuItem {
            objectName: "clipMenu:pasteTransition"
            text: qsTr("Paste the transition into this clip")
            enabled: menus.project.hasCopiedTransition
            visible: clipMenu.track !== null && clipMenu.track.kind !== "title"
            onTriggered: menus.project.pasteTransition(clipMenu.forClip.id)
        }
        AppMenu {
            id: speedMenu
            objectName: "clipMenu:speed"
            title: qsTr("Speed")
            enabled: menus.hasSpeed
            Repeater {
                model: [0.25, 0.5, 1, 2, 4]
                delegate: AppMenuItem {
                    required property real modelData
                    objectName: "clipMenu:speed:" + modelData
                    text: Fmt.brief(modelData, 2) + "×"
                    checkable: true
                    checked: clipMenu.forClip !== null && Math.abs((clipMenu.forClip.speed || 1) - modelData) < 0.001
                    onTriggered: menus.project.setClipSpeed(clipMenu.forClip.id, modelData, clipMenu.forClip.reversed === true)
                }
            }
            AppMenuSeparator {}
            AppMenuItem {
                objectName: "clipMenu:reverse"
                text: qsTr("Reverse")
                checkable: true
                checked: clipMenu.forClip !== null && clipMenu.forClip.reversed === true
                onTriggered: menus.project.setClipSpeed(clipMenu.forClip.id, clipMenu.forClip.speed || 1, clipMenu.forClip.reversed !== true)
            }
            AppMenuItem {
                objectName: "clipMenu:freeze"
                text: qsTr("Freeze frame at the playhead")
                enabled: menus.playheadInClip && clipMenu.track !== null && clipMenu.track.kind === "video"
                onTriggered: menus.project.freezeFrame(2)
            }
        }
        AppMenuSeparator {}
        AppMenuItem {
            objectName: "clipMenu:group"
            text: qsTr("Group"); shortcut: KeyMap.text("group")
            enabled: menus.project.selectedClips.length > 1
            onTriggered: menus.project.groupSelected()
        }
        AppMenuItem {
            objectName: "clipMenu:ungroup"
            text: qsTr("Ungroup"); shortcut: KeyMap.text("ungroup")
            enabled: clipMenu.forClip !== null && clipMenu.forClip.groupId > 0
            onTriggered: menus.project.ungroupSelected()
        }
        AppMenuItem {
            objectName: "clipMenu:compound"
            text: clipMenu.forClip !== null && clipMenu.forClip.isCompound === true ? qsTr("Break apart the compound clip")
                                                                            : qsTr("Make a compound clip")
            onTriggered: clipMenu.forClip.isCompound === true ? menus.project.breakCompound(clipMenu.forClip.id)
                                                           : menus.project.createCompound()
        }
        AppMenuItem {
            objectName: "clipMenu:marker"
            text: qsTr("Add a marker at the playhead"); shortcut: KeyMap.text("addMarker")
            onTriggered: menus.project.addMarker()
        }
        AppMenuSeparator {}
        AppMenuItem {
            objectName: "clipMenu:copyGrade"
            text: qsTr("Copy the colour grade")
            visible: clipMenu.track !== null && clipMenu.track.kind === "video" && clipMenu.forClip !== null && clipMenu.forClip.isTitle !== true
            onTriggered: menus.project.copyGrade(clipMenu.forClip.id)
        }
        AppMenuItem {
            objectName: "clipMenu:pasteGrade"
            text: qsTr("Paste the colour grade")
            visible: clipMenu.track !== null && clipMenu.track.kind === "video" && clipMenu.forClip !== null && clipMenu.forClip.isTitle !== true
            onTriggered: menus.project.pasteGrade(clipMenu.forClip.id)
        }
        AppMenuItem {
            objectName: "clipMenu:selectTrack"
            text: qsTr("Select every clip on this track")
            onTriggered: menus.project.selectAllClips(clipMenu.track.id)
        }
        AppMenuItem {
            objectName: "clipMenu:reveal"
            text: qsTr("Show the file in Finder")
            visible: clipMenu.forClip !== null && clipMenu.forClip.mediaId > 0
            onTriggered: menus.project.revealMedia(clipMenu.forClip.mediaId)
        }
    }

    AppMenu {
        id: trackMenu
        objectName: "trackMenu"
        property var track: null

        AppMenuItem { objectName: "trackMenu:addVideo"; text: qsTr("Add a video track"); onTriggered: menus.addTrack("video", trackMenu.track) }
        AppMenuItem { objectName: "trackMenu:addAudio"; text: qsTr("Add an audio track"); onTriggered: menus.addTrack("audio", trackMenu.track) }
        AppMenuItem { objectName: "trackMenu:addTitle"; text: qsTr("Add a title track"); onTriggered: menus.addTrack("title", trackMenu.track) }
        AppMenuSeparator {}
        AppMenuItem {
            objectName: "trackMenu:rename"
            text: qsTr("Rename…")
            onTriggered: {
                renameBox.track = trackMenu.track
                renameBox.open()
            }
        }
        AppMenuItem {
            objectName: "trackMenu:lock"
            text: trackMenu.track !== null && trackMenu.track.locked ? qsTr("Unlock") : qsTr("Lock")
            onTriggered: menus.project.setTrackFlag(trackMenu.track.id, "locked", !trackMenu.track.locked)
        }
        AppMenuItem {
            objectName: "trackMenu:hide"
            visible: trackMenu.track !== null && trackMenu.track.kind !== "audio"
            text: trackMenu.track !== null && trackMenu.track.hidden ? qsTr("Show") : qsTr("Hide")
            onTriggered: menus.project.setTrackFlag(trackMenu.track.id, "hidden", !trackMenu.track.hidden)
        }
        AppMenuItem {
            objectName: "trackMenu:mute"
            text: trackMenu.track !== null && trackMenu.track.muted ? qsTr("Unmute") : qsTr("Mute")
            onTriggered: menus.project.setTrackFlag(trackMenu.track.id, "muted", !trackMenu.track.muted)
        }
        AppMenuItem {
            objectName: "trackMenu:closeGaps"
            text: qsTr("Close the gaps between its clips")
            enabled: trackMenu.track !== null && trackMenu.track.clips.length > 1
            onTriggered: menus.project.closeGaps(trackMenu.track.id)
        }
        TrackHeightMenu { objectName: "trackMenu:height" }
        AppMenuItem {
            objectName: "trackMenu:selectAll"
            text: qsTr("Select every clip on this track")
            enabled: trackMenu.track !== null && trackMenu.track.clips.length > 0
            onTriggered: menus.project.selectAllClips(trackMenu.track.id)
        }
        AppMenuSeparator {}
        AppMenuItem {
            objectName: "trackMenu:delete"
            text: qsTr("Delete this track")
            enabled: trackMenu.track !== null && !trackMenu.track.locked
                     && !(trackMenu.track.kind === "video" && menus.project.tracks.filter((t) => t.kind === "video").length === 1)
            onTriggered: menus.deleteTrack(trackMenu.track)
        }
        AppMenuItem { objectName: "trackMenu:deleteEmpty"; text: qsTr("Delete empty tracks"); onTriggered: menus.project.removeEmptyTracks() }
    }

    AppMenu {
        id: laneMenu
        objectName: "laneMenu"
        property var track: null
        property real seconds: 0

        AppMenuItem {
            objectName: "laneMenu:paste"
            text: qsTr("Paste here"); shortcut: KeyMap.text("paste")
            enabled: menus.project.canPaste
            onTriggered: menus.project.pasteClips(laneMenu.track ? laneMenu.track.id : -1, laneMenu.seconds)
        }
        AppMenuItem {
            objectName: "laneMenu:marker"
            text: qsTr("Add a marker here")
            onTriggered: menus.project.addMarker(laneMenu.seconds)
        }
        AppMenuItem {
            objectName: "laneMenu:closeGap"
            text: qsTr("Close this gap")
            enabled: laneMenu.track !== null
            onTriggered: menus.project.closeGapAt(laneMenu.track.id, laneMenu.seconds)
        }
        AppMenuItem {
            objectName: "laneMenu:closeGaps"
            text: qsTr("Close every gap on this track")
            enabled: laneMenu.track !== null && laneMenu.track.clips.length > 1
            onTriggered: menus.project.closeGaps(laneMenu.track.id)
        }
        AppMenuSeparator {}
        AppMenuItem {
            objectName: "laneMenu:playRange"
            text: qsTr("Play from In to Out")
            enabled: menus.project.markIn >= 0 || menus.project.markOut >= 0
            onTriggered: menus.project.playMarkedRange()
        }
        AppMenuItem {
            objectName: "laneMenu:deleteRange"
            text: qsTr("Delete from In to Out")
            enabled: menus.project.markIn >= 0 && menus.project.markOut >= 0
            onTriggered: menus.project.deleteMarkedRange()
        }
        AppMenuItem {
            objectName: "laneMenu:clearMarks"
            text: qsTr("Clear In and Out"); shortcut: KeyMap.text("clearMarks")
            enabled: menus.project.markIn >= 0 || menus.project.markOut >= 0
            onTriggered: menus.project.clearMarks()
        }
        AppMenuSeparator {}
        AppMenuItem {
            objectName: "laneMenu:selectAll"
            text: qsTr("Select every clip"); shortcut: KeyMap.text("selectAll")
            onTriggered: menus.project.selectAllClips()
        }
        AppMenuSeparator {}
        AppMenuItem { objectName: "laneMenu:addVideo"; text: qsTr("Add a video track"); onTriggered: menus.addTrack("video", laneMenu.track) }
        AppMenuItem { objectName: "laneMenu:addAudio"; text: qsTr("Add an audio track"); onTriggered: menus.addTrack("audio", laneMenu.track) }
        AppMenuItem { objectName: "laneMenu:addTitle"; text: qsTr("Add a title track"); onTriggered: menus.addTrack("title", laneMenu.track) }
        AppMenuItem { objectName: "laneMenu:deleteEmpty"; text: qsTr("Delete empty tracks"); onTriggered: menus.project.removeEmptyTracks() }
        TrackHeightMenu { objectName: "laneMenu:height" }
    }

    // Small, normal or large tracks (the whole timeline).
    component TrackHeightMenu: AppMenu {
        title: qsTr("Track height")
        Repeater {
            model: [{ key: "small", label: qsTr("Small"), scale: 0.7 }, { key: "normal", label: qsTr("Normal"), scale: 1.0 },
                    { key: "large", label: qsTr("Large"), scale: 1.5 }]
            delegate: AppMenuItem {
                required property var modelData
                objectName: "trackHeight:" + modelData.key
                text: modelData.label
                checkable: true
                checked: menus.timeline !== null && Math.abs(menus.timeline.trackScale - modelData.scale) < 0.01
                onTriggered: if (menus.timeline) menus.timeline.trackScale = modelData.scale
            }
        }
    }

    AppMenu {
        id: transitionMenu
        objectName: "transitionMenu"
        property var transition: null
        AppMenuItem {
            objectName: "transitionMenu:copy"
            text: qsTr("Copy the transition")
            onTriggered: menus.project.copyTransition(transitionMenu.transition.clipId)
        }
        AppMenuItem {
            objectName: "transitionMenu:remove"
            text: qsTr("Remove the transition")
            onTriggered: menus.project.removeTransition(transitionMenu.transition.clipId)
        }
    }

    // Which attributes to paste onto the selected clips.
    Popup {
        id: pasteBox
        objectName: "pasteAttributes"
        modal: true
        focus: true
        anchors.centerIn: Overlay.overlay
        width: 380
        padding: Theme.space.l
        readonly property var parts: [{ key: "visual", label: qsTr("Position, size and crop") }, { key: "color", label: qsTr("Colour and look") },
                                      { key: "effects", label: qsTr("Effects") }, { key: "keying", label: qsTr("Chroma key and masks") },
                                      { key: "audio", label: qsTr("Volume, fades and sound effects") }, { key: "speed", label: qsTr("Speed") }]
        property var chosen: ["visual", "color", "effects", "keying", "audio"]
        Overlay.modal: Rectangle { color: Qt.alpha("#000000", Theme.dark ? 0.55 : 0.35) }
        background: Rectangle {
            radius: Theme.radius.l
            color: Theme.color.surfaceRaised
            border.color: Theme.color.borderStrong
        }
        contentItem: ColumnLayout {
            spacing: Theme.space.s
            Text { text: qsTr("Paste attributes"); font: Theme.type.title; color: Theme.color.textPrimary }
            Text {
                Layout.fillWidth: true
                text: qsTr("Onto the selected clips, from the clip you copied them from:")
                font: Theme.type.body
                color: Theme.color.textSecondary
                wrapMode: Text.WordWrap
            }
            Repeater {
                model: pasteBox.parts
                delegate: CheckBox {
                    required property var modelData
                    objectName: "pastePart:" + modelData.key
                    text: modelData.label
                    font: Theme.type.body
                    checked: pasteBox.chosen.indexOf(modelData.key) >= 0
                    onToggled: pasteBox.chosen = checked ? pasteBox.chosen.concat([modelData.key])
                                                         : pasteBox.chosen.filter((k) => k !== modelData.key)
                }
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: Theme.space.s
                TextButton { text: qsTr("Cancel"); onClicked: pasteBox.close() }
                TextButton {
                    objectName: "pasteAttributesAccept"
                    text: qsTr("Paste")
                    variant: "primary"
                    enabled: pasteBox.chosen.length > 0
                    onClicked: {
                        if (menus.project.pasteAttributes(pasteBox.chosen))
                            pasteBox.close()
                    }
                }
            }
        }
    }

    ConfirmDialog { id: confirm; objectName: "timelineConfirm" }

    Popup {
        id: renameBox
        objectName: "renameTrack"
        property var track: null
        modal: true
        focus: true
        anchors.centerIn: Overlay.overlay
        width: 360
        padding: Theme.space.l
        onOpened: {
            nameField.text = track ? track.name : ""
            nameField.selectAll()
            nameField.forceActiveFocus()
        }
        function accept() {
            if (track && menus.project.renameTrack(track.id, nameField.text))
                close()
        }
        Overlay.modal: Rectangle { color: Qt.alpha("#000000", Theme.dark ? 0.55 : 0.35) }
        background: Rectangle {
            radius: Theme.radius.l
            color: Theme.color.surfaceRaised
            border.color: Theme.color.borderStrong
        }
        contentItem: ColumnLayout {
            spacing: Theme.space.m
            Text { text: qsTr("Rename the track"); font: Theme.type.title; color: Theme.color.textPrimary }
            TextField {
                id: nameField
                objectName: "renameTrackField"
                Layout.fillWidth: true
                maximumLength: 64
                font: Theme.type.body
                onAccepted: renameBox.accept()
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: Theme.space.s
                TextButton { text: qsTr("Cancel"); onClicked: renameBox.close() }
                TextButton { objectName: "renameTrackAccept"; text: qsTr("Rename"); variant: "primary"; onClicked: renameBox.accept() }
            }
        }
    }
}
