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

    // Match frame: the editor opens the clip's media in the source monitor at the frame.
    signal matchFrameRequested(double clipId)

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
        let id = 0
        if (kind === "audio") {
            const tracks = project.tracks // top to bottom
            const below = beside && beside.kind === "audio" ? beside : tracks[tracks.length - 1]
            id = project.addTrack("audio", below ? below.id : -1, false)
        } else {
            id = project.addTrack(kind, beside && beside.kind !== "audio" ? beside.id : -1, true)
        }
        // The new track scrolled into view (once the lanes have it).
        if (id > 0)
            Qt.callLater(() => menus.timeline.showTrack(id))
        return id
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

    // Media that can take a clip's place: pictures on a picture track, sound on a sound track.
    readonly property var replacements: clipMenu.forClip === null || clipMenu.track === null
                                        || clipMenu.forClip.mediaId <= 0 || clipMenu.forClip.isCompound === true ? []
        : project.media.filter((m) => m.id !== clipMenu.forClip.mediaId && !m.missing && !m.sticker
                                      && (clipMenu.track.kind === "audio" ? m.hasAudio : m.hasVideo)).slice(0, 40)
    readonly property bool pictureClip: clipMenu.forClip !== null && clipMenu.track !== null && clipMenu.track.kind !== "audio"

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
        AppMenuItem {
            objectName: "clipMenu:extendEdit"
            text: qsTr("Extend the nearer edge to the playhead"); shortcut: KeyMap.text("extendEdit")
            onTriggered: menus.project.extendEdit(clipMenu.forClip.id)
        }
        AppMenuSeparator {}
        AppMenuItem {
            objectName: "clipMenu:enabled"
            text: clipMenu.forClip !== null && clipMenu.forClip.enabled === false ? qsTr("Turn on") : qsTr("Turn off")
            shortcut: KeyMap.text("toggleClip")
            onTriggered: menus.project.setClipEnabled(clipMenu.forClip.id, clipMenu.forClip.enabled === false)
        }
        AppMenu {
            id: replaceMenu
            objectName: "clipMenu:replace"
            title: qsTr("Replace with")
            enabled: menus.replacements.length > 0
            Instantiator {
                model: menus.replacements
                delegate: AppMenuItem {
                    required property var modelData
                    objectName: "clipMenu:replace:" + modelData.id
                    text: modelData.name
                    onTriggered: menus.project.replaceClipMedia(clipMenu.forClip.id, modelData.id)
                }
                onObjectAdded: (index, object) => replaceMenu.insertItem(index, object)
                onObjectRemoved: (index, object) => replaceMenu.removeItem(object)
            }
        }
        AppMenuItem {
            objectName: "clipMenu:matchFrame"
            text: qsTr("Match frame (open the media here)"); shortcut: KeyMap.text("matchFrame")
            visible: clipMenu.forClip !== null && clipMenu.forClip.mediaId > 0 && clipMenu.forClip.isCompound !== true
            enabled: menus.playheadInClip || (clipMenu.forClip !== null && Math.abs(menus.project.playhead - clipMenu.forClip.start) < 0.001)
            onTriggered: menus.matchFrameRequested(clipMenu.forClip.id)
        }
        AppMenu {
            objectName: "clipMenu:rotate"
            title: qsTr("Rotate and flip")
            enabled: menus.pictureClip
            AppMenuItem { objectName: "clipMenu:rotateRight"; text: qsTr("Rotate 90° clockwise"); onTriggered: menus.project.rotateClip(clipMenu.forClip.id, 90) }
            AppMenuItem { objectName: "clipMenu:rotateLeft"; text: qsTr("Rotate 90° anticlockwise"); onTriggered: menus.project.rotateClip(clipMenu.forClip.id, -90) }
            AppMenuItem { objectName: "clipMenu:rotate180"; text: qsTr("Rotate 180°"); onTriggered: menus.project.rotateClip(clipMenu.forClip.id, 180) }
            AppMenuSeparator {}
            AppMenuItem {
                objectName: "clipMenu:flipH"
                text: qsTr("Flip left to right")
                checkable: true
                checked: clipMenu.forClip !== null && clipMenu.forClip.flipH === true
                onTriggered: menus.project.flipClip(clipMenu.forClip.id, true)
            }
            AppMenuItem {
                objectName: "clipMenu:flipV"
                text: qsTr("Flip upside down")
                checkable: true
                checked: clipMenu.forClip !== null && clipMenu.forClip.flipV === true
                onTriggered: menus.project.flipClip(clipMenu.forClip.id, false)
            }
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
            objectName: "clipMenu:openCompound"
            text: clipMenu.forClip !== null && clipMenu.forClip.isMulticam === true ? qsTr("Open this angle in the timeline")
                                                                              : qsTr("Open in the timeline")
            visible: clipMenu.forClip !== null && clipMenu.forClip.isCompound === true
            onTriggered: menus.project.openCompoundClip(clipMenu.forClip.id)
        }
        AppMenuItem {
            objectName: "clipMenu:multicam"
            text: qsTr("Make a multicam clip")
            visible: clipMenu.forClip !== null && clipMenu.forClip.isMulticam !== true
            enabled: menus.project.selectedClips.length > 1
            onTriggered: menus.project.makeMulticam()
        }
        AppMenu {
            id: anglesMenu
            objectName: "clipMenu:angles"
            title: qsTr("Angle")
            enabled: clipMenu.forClip !== null && clipMenu.forClip.isMulticam === true
            Instantiator {
                model: clipMenu.forClip !== null && clipMenu.forClip.isMulticam === true ? clipMenu.forClip.angles : []
                delegate: AppMenuItem {
                    required property var modelData
                    objectName: "clipMenu:angle:" + modelData.index
                    text: (modelData.index + 1) + "  " + modelData.name
                    checkable: true
                    checked: clipMenu.forClip !== null && clipMenu.forClip.angle === modelData.index
                    shortcut: KeyMap.text("angle" + (modelData.index + 1))
                    onTriggered: menus.project.setClipAngle(clipMenu.forClip.id, modelData.index)
                }
                onObjectAdded: (index, object) => anglesMenu.insertItem(index, object)
                onObjectRemoved: (index, object) => anglesMenu.removeItem(object)
            }
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
            objectName: "trackMenu:moveUp"
            text: qsTr("Move up")
            onTriggered: menus.project.moveTrack(trackMenu.track.id, 1)
        }
        AppMenuItem {
            objectName: "trackMenu:moveDown"
            text: qsTr("Move down")
            onTriggered: menus.project.moveTrack(trackMenu.track.id, -1)
        }
        AppMenuItem {
            objectName: "trackMenu:lock"
            text: trackMenu.track !== null && trackMenu.track.locked ? qsTr("Unlock") : qsTr("Lock")
            onTriggered: menus.project.setTrackFlag(trackMenu.track.id, "locked", !trackMenu.track.locked)
        }
        AppMenuItem {
            objectName: "trackMenu:lockAll"
            text: qsTr("Lock every track")
            enabled: menus.project.tracks.some((t) => !t.locked)
            onTriggered: menus.project.lockAllTracks(true)
        }
        AppMenuItem {
            objectName: "trackMenu:unlockAll"
            text: qsTr("Unlock every track")
            enabled: menus.project.tracks.some((t) => t.locked)
            onTriggered: menus.project.lockAllTracks(false)
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
            objectName: "laneMenu:colourClip"
            text: qsTr("Add a colour clip here")
            onTriggered: {
                menus.project.seek(laneMenu.seconds)
                menus.project.addColorClip({ kind: "solid", from: [0.12, 0.44, 0.92, 1] })
            }
        }
        AppMenuItem {
            objectName: "laneMenu:placeholder"
            text: qsTr("Add a placeholder here")
            onTriggered: {
                menus.project.seek(laneMenu.seconds)
                menus.project.addPlaceholder()
            }
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
        AppMenuItem {
            objectName: "laneMenu:selectForward"
            text: qsTr("Select from the playhead on"); shortcut: KeyMap.text("selectForward")
            onTriggered: menus.project.selectFromPlayhead(1)
        }
        AppMenuItem {
            objectName: "laneMenu:selectBackward"
            text: qsTr("Select up to the playhead"); shortcut: KeyMap.text("selectBackward")
            onTriggered: menus.project.selectFromPlayhead(-1)
        }
        AppMenuSeparator {}
        AppMenuItem {
            objectName: "laneMenu:deleteTransitions"
            text: qsTr("Delete every transition…")
            onTriggered: confirm.ask(qsTr("Delete every transition?"), qsTr("Every cut becomes a straight cut. You can undo this."),
                                     qsTr("Delete transitions"), () => menus.project.removeAllTransitions())
        }
        AppMenuItem {
            objectName: "laneMenu:deleteMarkers"
            text: qsTr("Delete every marker…")
            enabled: menus.project.markers.length > 0
            onTriggered: confirm.ask(qsTr("Delete every marker?"), qsTr("Chapters made from them go too. You can undo this."),
                                     qsTr("Delete markers"), () => menus.project.removeAllMarkers())
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
