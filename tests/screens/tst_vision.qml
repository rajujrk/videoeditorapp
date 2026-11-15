import QtQuick
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell
import "screens.js" as Screens

// Analysis on this Mac: flicker removed and a title following a tracked card, a face covered,
// and an object removed.
Item {
    id: root
    width: 1470
    height: 790
    Component {
        id: editorComponent
        Item {
            property alias project: controller
            property alias editor: screen
            anchors.fill: parent
            ProjectController { id: controller; view: screen.player; autosaveEnabled: false }
            EditorScreen { id: screen; anchors.fill: parent; project: controller }
        }
    }
    SignalSpy { id: finished; signalName: "analysisFinished" }

    TestCase {
        name: "ScreensVision"
        when: windowShown
        readonly property string samples: "file://" + samplesDir + "/"

        function editor() {
            const f = createTemporaryObject(editorComponent, root)
            waitForRendering(f)
            finished.clear(); finished.target = f.project
            return f
        }
        function main(p) { return p.tracks.find((t) => t.main) }
        function shot(name, dark, settle) {
            mouseMove(root, root.width - 4, root.height - 4)
            wait(settle === undefined ? 1500 : settle)
            grabImage(root.Window.window.contentItem).save(Screens.file(screensDir, name, dark))
        }
        function scrollTo(f, name) {
            const item = findChild(f.editor, name)
            verify(item, name)
            for (let a = item.parent; a; a = a.parent) {
                if (a.contentY === undefined || a.contentHeight === undefined)
                    continue
                a.contentY = Math.max(0, Math.min(item.mapToItem(a.contentItem, 0, 0).y - 12, a.contentHeight - a.height))
                return
            }
        }
        function done(count) {
            tryVerify(() => finished.count >= count, 180000, "the analysis never finished")
            verify(finished.signalArguments[count - 1][0], finished.signalArguments[count - 1][1])
        }
        function withSample(f, file) {
            const p = f.project
            verify(p.importAndAppend(samples + file), "" + p.lastError)
            const id = Number(main(p).clips[0].id)
            p.selectedClip = id
            p.seek(0)
            return id
        }

        function test_scenes() {
            for (const dark of [true, false]) {
                Theme.dark = dark
                EditingPreferences.reset()
                Screens.palette(root.Window.window, Theme)

                // Steady and clean: flicker removed; a title following the tracked card.
                let f = editor()
                let p = f.project
                let id = withSample(f, "card.mp4")
                p.trackAreaKind = "surface"
                verify(p.setTrackArea(id, { x: 0.375, y: 0.528, width: 0.27, height: 0.34 }), "" + p.lastError)
                verify(p.trackMotion(id), "" + p.lastError)
                done(1)
                verify(p.removeFlicker(id), "" + p.lastError)
                done(2)
                verify(p.addTitle({ text: "Follow me", size: 0.07, bold: true }, -1, 0), "" + p.lastError)
                const title = Number(p.selectedClip)
                verify(p.followTrack(title, id, p.tracksToFollow(title)[0].trackId, { rotation: true }), "" + p.lastError)
                p.selectedClip = id
                p.seek(1.6)
                findChild(f.editor, "inspectorTab:Analyze").clicked()
                wait(300)
                scrollTo(f, "removeFlicker")
                shot("motion", dark)
                f.destroy(); wait(200)

                // A face found and covered with a mosaic that follows it.
                f = editor()
                p = f.project
                id = withSample(f, "people.mp4")
                verify(p.findFaces(id), "" + p.lastError)
                done(1)
                verify(p.coverFaces(id, [p.faces.tracks[0].id], { style: "mosaic" }), "" + p.lastError)
                p.selectedClip = id
                p.seek(1.5)
                findChild(f.editor, "inspectorTab:Analyze").clicked()
                wait(300)
                scrollTo(f, "facesCovered")
                shot("people", dark)
                f.destroy(); wait(200)

                // The moving card removed, filled in from other frames and around it.
                f = editor()
                p = f.project
                id = withSample(f, "card.mp4")
                verify(p.setTrackArea(id, { x: 0.375, y: 0.528, width: 0.29, height: 0.37 }), "" + p.lastError)
                verify(p.processVideo(id, { kind: "remove" }), "" + p.lastError)
                done(1)
                p.selectedClip = id
                p.seek(1.2)
                findChild(f.editor, "inspectorTab:Analyze").clicked()
                wait(300)
                scrollTo(f, "pictureSection")
                shot("vision", dark)
                f.destroy(); wait(200)
            }
            EditingPreferences.reset()
            Theme.dark = true
        }
    }
}
