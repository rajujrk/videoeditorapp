import QtQuick
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell
import "screens.js" as Screens

// The Color tab (wheels, curves and a LUT, scopes, HSL), the keyframe curve editor, looks and

// an adjustment layer, 360° reframing, and speed.
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
    SignalSpy { id: exported; signalName: "exportFinished" }

    TestCase {
        name: "ScreensGrading"
        when: windowShown
        readonly property string media: "file://" + screensMediaDir + "/"
        readonly property string samples: "file://" + samplesDir + "/"

        function editor() {
            const f = createTemporaryObject(editorComponent, root)
            waitForRendering(f)
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
        // The portrait, graded teal and orange.
        function graded(f) {
            const p = f.project
            verify(p.importAndAppend(media + "night-street.mp4"), "" + p.lastError)
            const clip = Number(main(p).clips[0].id)
            verify(p.setClipColor(clip, {
                lift: { x: -0.28, y: -0.12, master: 0 },
                gain: { x: 0.22, y: 0.14, master: 0.05 },
                contrast: 0.15, saturation: 0.1,
                red: [{ x: 0, y: 0 }, { x: 0.25, y: 0.17 }, { x: 0.75, y: 0.84 }, { x: 1, y: 1 }]
            }), "" + p.lastError)
            p.selectedClip = clip
            p.seek(1.5)
            findChild(f.editor, "inspectorTab:Color").clicked()
            return clip
        }

        function test_scenes() {
            for (const dark of [true, false]) {
                Theme.dark = dark
                EditingPreferences.reset()
                Screens.palette(root.Window.window, Theme)

                // The grade on the wheels, then on the curves with a LUT at 35 %.

                let f = editor()
                let p = f.project
                let clip = graded(f)
                wait(300)
                scrollTo(f, "wheelsSection")
                shot("color", dark)
                verify(p.chooseLut(clip, "qrc" + p.lutLibrary[0].path), "" + p.lastError)
                verify(p.setClipColor(clip, { lutIntensity: 0.35 }), "" + p.lastError)
                wait(200)
                scrollTo(f, "curvesSection")
                mouseClick(findChild(f.editor, "curveChannel:red"))
                shot("color-curves", dark)

                // Scopes: the vectorscope, then the RGB parade; HSL with the blue band chosen.

                verify(p.setClipColor(clip, { hsl: [{ band: 5, hue: 0.06, saturation: 0.25 }, { band: 1, saturation: 0.2 }] }), "" + p.lastError)
                scrollTo(f, "scopesSection")
                findChild(f.editor, "scopeMode:vectorscope").clicked()
                shot("scopes", dark)
                findChild(f.editor, "scopeMode:parade").clicked()
                shot("scopes-parade", dark)
                scrollTo(f, "hslSection")
                wait(300)
                mouseClick(findChild(f.editor, "hslBand:5"))
                shot("hsl", dark)
                findChild(f.editor, "scopeMode:waveform").clicked()
                f.destroy(); wait(200)

                // The keyframe curve editor: an Overshoot curve on the keyframe at the playhead.

                f = editor()
                p = f.project
                verify(p.importAndAppend(media + "night-street.mp4"), "" + p.lastError)
                clip = Number(main(p).clips[0].id)
                p.selectedClip = clip
                p.seek(0.5)
                verify(p.setClipVisual(clip, { scale: 0.7 }), "" + p.lastError)
                verify(p.addKeyframes(clip, ["scale"]), "" + p.lastError)
                p.seek(2)
                verify(p.setClipVisual(clip, { scale: 1 }), "" + p.lastError) // (keyed: animated)
                p.seek(0.5)
                verify(p.setKeyframeEasing(clip, "curve"), "" + p.lastError)
                verify(p.setKeyframeCurve(clip, 0.34, 1.56, 0.64, 1), "" + p.lastError)
                findChild(f.editor, "inspectorTab:Video").clicked()
                wait(300)
                scrollTo(f, "curveEditor")
                shot("curves", dark)
                f.destroy(); wait(200)

                // Looks: Teal & Orange on the clip, the Filters tab and the Look section.

                f = editor()
                p = f.project
                verify(p.importAndAppend(media + "night-street.mp4"), "" + p.lastError)
                verify(p.importAndAppend(media + "portrait-wide.mp4"), "" + p.lastError)
                clip = Number(main(p).clips[0].id)
                verify(p.applyLook(clip, "teal_orange"), "" + p.lastError)
                p.selectedClip = clip
                p.seek(1.5)
                findChild(f.editor, "libraryTab:Filters").clicked()
                findChild(f.editor, "inspectorTab:Color").clicked()
                wait(300)
                scrollTo(f, "lookSection")
                shot("filters", dark)

                // An adjustment layer: Golden Hour, the Viewfinder overlay and Film Grain over both clips.

                p.seek(0)
                verify(p.addAdjustmentLayer(-1, 0), "" + p.lastError)
                const layer = Number(p.selectedClip)
                verify(p.applyLook(layer, "golden_hour"), "" + p.lastError)
                verify(p.addClipEffect(layer, "tve.overlay.viewfinder"), "" + p.lastError)
                verify(p.addClipEffect(layer, "tve.overlay.film_grain"), "" + p.lastError)
                p.selectedClip = layer
                p.seek(3.5)
                findChild(f.editor, "inspectorTab:Effects").clicked()
                shot("adjustment", dark)
                f.destroy(); wait(200)

                // 360°: a 360° file (made from a 360° project) reframed in a flat project.

                f = editor()
                p = f.project
                verify(p.importAndAppend(samples + "test_av_1080p30.mp4"), "" + p.lastError)
                verify(p.setSphericalProject(true), "" + p.lastError)
                const sphere = tempDir + "/tve-screens-360-" + (dark ? "d" : "l") + ".mp4"
                exported.clear(); exported.target = p
                verify(p.exportVideo("file://" + sphere, { format: "mp4-h264", range: "custom", from: 0, to: 2 }), "" + p.lastError)
                exported.wait(120000)
                f.destroy(); wait(200)
                f = editor()
                p = f.project
                verify(p.importAndAppend("file://" + sphere), "" + p.lastError)
                verify(p.setSphericalProject(false), "" + p.lastError)
                clip = Number(main(p).clips[0].id)
                p.selectedClip = clip
                verify(p.setClipVisual(clip, { yaw: 35, pitch: 18, fov: 90 }), "" + p.lastError)
                p.seek(1)
                findChild(f.editor, "inspectorTab:Video").clicked()
                shot("sphere-360", dark)
                f.destroy(); wait(200)

                // Speed: the Speed tab, then the Hero moment ramp.

                f = editor()
                p = f.project
                verify(p.importAndAppend(samples + "test_av_1080p30.mp4"), "" + p.lastError)
                verify(p.importAndAppend(media + "night-street.mp4"), "" + p.lastError)
                clip = Number(main(p).clips[0].id)
                verify(p.setClipSpeed(clip, 1.5, false), "" + p.lastError)
                p.selectedClip = clip
                p.seek(1)
                findChild(f.editor, "inspectorTab:Speed").clicked()
                shot("speed", dark)
                verify(p.applySpeedRampPreset(clip, "hero-moment"), "" + p.lastError)
                p.seek(1.2)
                shot("speed-ramp", dark)
                f.destroy(); wait(200)
            }
            EditingPreferences.reset()
            Theme.dark = true
        }
    }
}
