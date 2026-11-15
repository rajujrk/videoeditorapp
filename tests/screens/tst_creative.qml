import QtQuick
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell
import "screens.js" as Screens

// Effects (the library, frame effects, an animated setting), masks, chroma key and split
// screens, stickers, transitions, titles, and drawing on the player with an overlay.
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

    TestCase {
        name: "ScreensCreative"
        when: windowShown
        readonly property string media: "file://" + screensMediaDir + "/"
        readonly property string samples: "file://" + samplesDir + "/"

        function editor() {
            const f = createTemporaryObject(editorComponent, root)
            waitForRendering(f)
            return f
        }
        function main(p) { return p.tracks.find((t) => t.main) }
        function above(p) { return p.tracks.find((t) => t.name === "Video 2") }
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
        function tab(f, library, inspector) {
            if (library) findChild(f.editor, "libraryTab:" + library).clicked()
            if (inspector) findChild(f.editor, "inspectorTab:" + inspector).clicked()
        }
        // A title template from the Titles tab, its category chosen first (tiles are made as shown).
        function template(f, category, name) {
            chip(f, "titleCategory:" + category)
            tryVerify(() => findChild(f.editor, "libraryTitle:" + name) !== null, 3000, name)
            mouseClick(findChild(f.editor, "libraryTitle:" + name))
            // (Off the tile, so its tooltip goes; nothing stays selected to be restyled next.)
            mouseMove(root, root.width / 2, 30)
            mouseMove(root, root.width - 4, root.height - 4)
            wait(300)
        }
        function chip(f, name) {
            const c = findChild(f.editor, name)
            verify(c, name)
            c.clicked()
        }

        function test_scenes() {
            for (const dark of [true, false]) {
                Theme.dark = dark
                EditingPreferences.reset()
                Screens.palette(root.Window.window, Theme)

                // The Effects tab, Halftone on the clip.
                let f = editor()
                let p = f.project
                verify(p.importAndAppend(media + "night-street.mp4"), "" + p.lastError)
                let clip = Number(main(p).clips[0].id)
                verify(p.addClipEffect(clip, "tve.stylize.halftone"), "" + p.lastError)
                p.selectedClip = clip
                p.seek(1.5)
                tab(f, "Effects", "Effects")
                shot("effects-library", dark)

                // Frame effects on a picture in picture: Rounded Corners, Outline, Drop Shadow.
                verify(p.removeClipEffect(clip, 0), "" + p.lastError)
                verify(p.importAndAppend(media + "portrait-wide.mp4"), "" + p.lastError)
                const pip = Number(main(p).clips[1].id)
                verify(p.moveClip(pip, above(p).id, 0), "" + p.lastError)
                verify(p.setClipVisual(pip, { scale: 0.42, x: 0.26, y: -0.22 }), "" + p.lastError)
                verify(p.addClipEffect(pip, "tve.frame.rounded_corners"), "" + p.lastError)
                verify(p.addClipEffect(pip, "tve.frame.outline"), "" + p.lastError)
                verify(p.addClipEffect(pip, "tve.frame.drop_shadow"), "" + p.lastError)
                p.selectedClip = pip
                p.seek(1)
                chip(f, "effectCategory:frame")
                shot("frame-effects", dark)
                f.destroy(); wait(200)

                // An animated effect setting: the blur's radius between two keyframes.
                f = editor()
                p = f.project
                verify(p.importAndAppend(media + "night-street.mp4"), "" + p.lastError)
                clip = Number(main(p).clips[0].id)
                verify(p.addClipEffect(clip, "tve.blur.gaussian"), "" + p.lastError)
                p.selectedClip = clip
                p.seek(0.3)
                verify(p.setClipEffectParam(clip, 0, "radius", 2), "" + p.lastError)
                verify(p.addKeyframes(clip, ["effect:0:radius"]), "" + p.lastError)
                p.seek(2.7)
                verify(p.setClipEffectParam(clip, 0, "radius", 30), "" + p.lastError) // (keyed: animated)
                p.seek(1.4)
                tab(f, "Effects", "Effects")
                shot("effect-keyframes", dark)
                f.destroy(); wait(200)

                // Masks: a feathered, turned ellipse and a star on the top clip.
                f = editor()
                p = f.project
                verify(p.importAndAppend(samples + "gradients_1080p30_h264.mp4"), "" + p.lastError)
                verify(p.importAndAppend(media + "night-street.mp4"), "" + p.lastError)
                const top = Number(main(p).clips[1].id)
                verify(p.moveClip(top, above(p).id, 0), "" + p.lastError)
                verify(p.addMask(top, "ellipse"), "" + p.lastError)
                verify(p.setMask(top, 0, { feather: 0.12, rotation: -18, width: 0.42, height: 0.6, x: 0.38, y: 0.48 }), "" + p.lastError)
                verify(p.addMask(top, "star"), "" + p.lastError)
                verify(p.setMask(top, 1, { width: 0.22, height: 0.3, x: 0.78, y: 0.3 }), "" + p.lastError)
                p.selectedClip = top
                p.seek(1.5)
                tab(f, "Effects", "Video")
                wait(300)
                scrollTo(f, "masksSection")
                shot("masks", dark)
                f.destroy(); wait(200)

                // Chroma key: a green screen keyed and scaled down over another clip.
                f = editor()
                p = f.project
                verify(p.importAndAppend(media + "night-street.mp4"), "" + p.lastError)
                compare(p.importMedia(["file://" + stillsDir + "/keyed.png"]), 1)
                verify(p.addClip(p.media[1].id, above(p).id, 0), "" + p.lastError)
                const keyed = Number(above(p).clips[0].id)
                verify(p.setChromaKey(keyed, { enabled: true, color: [0.1, 0.8, 0.25], tolerance: 0.3, softness: 0.1, spill: 0.8 }), "" + p.lastError)
                verify(p.setClipVisual(keyed, { scale: 0.55, x: 0.2, y: 0.1 }), "" + p.lastError)
                p.selectedClip = keyed
                p.seek(1)
                tab(f, "Media", "Video")
                wait(300)
                scrollTo(f, "chromaKeySection")
                findChild(f.editor, "chromaKeySection").expanded = true
                shot("chroma-key", dark)

                // Split screen: the two side by side.
                verify(p.setChromaKey(keyed, { enabled: false }), "" + p.lastError)
                verify(p.setClipVisual(keyed, { scale: 1, x: 0, y: 0 }), "" + p.lastError)
                p.seek(1)
                verify(p.applyLayout("side-by-side"), "" + p.lastError)
                p.selectedClip = keyed
                wait(300)
                findChild(f.editor, "layoutSection").expanded = true
                wait(300)
                scrollTo(f, "layoutSection")
                shot("split-screen", dark)
                f.destroy(); wait(200)

                // Stickers: five over a still, each on its own track.
                f = editor()
                p = f.project
                compare(p.importMedia([media + "photo.png"]), 1)
                verify(p.addClip(p.media[0].id), "" + p.lastError)
                p.seek(0)
                for (const s of ["sun_spin", "cloud_wobble", "heart_pulse", "star_bounce", "confetti_spin"])
                    verify(p.addSticker(s, -1, 0), s)
                const placed = [[-0.3, -0.25], [0.25, -0.28], [-0.32, 0.22], [0.05, 0.05], [0.33, 0.24]]
                let k = 0
                for (const t of p.tracks)
                    for (const c2 of t.clips)
                        if (!t.main && k < placed.length) {
                            verify(p.setClipVisual(Number(c2.id), { scale: 0.32, x: placed[k][0], y: placed[k][1] }), "" + p.lastError)
                            ++k
                        }
                p.seek(0.8)
                p.selectedClip = 0
                tab(f, "Stickers", "Video")
                shot("stickers", dark)
                f.destroy(); wait(200)

                // Transitions: a soft wipe; Cube on 3D; Clouds on Luma.
                f = editor()
                p = f.project
                verify(p.importAndAppend(samples + "test_av_1080p30.mp4"), "" + p.lastError)
                verify(p.importAndAppend(samples + "gradients_1080p30_h264.mp4"), "" + p.lastError)
                const second = Number(main(p).clips[1].id)
                verify(p.addTransition("tve.transition.wipe", second), "" + p.lastError)
                const cut = main(p).clips[1].start
                p.seek(cut)
                tab(f, "Transitions")
                shot("transitions", dark)
                verify(p.removeTransition(second), "" + p.lastError)
                verify(p.addTransition("tve.transition.cube", second), "" + p.lastError)
                p.seek(cut)
                chip(f, "transitionCategory:3d")
                shot("transitions-3d", dark)
                verify(p.removeTransition(second), "" + p.lastError)
                verify(p.addTransition("tve.transition.luma_clouds", second), "" + p.lastError)
                p.seek(cut)
                chip(f, "transitionCategory:luma")
                shot("transitions-luma", dark)
                f.destroy(); wait(200)

                // Titles: a title over gradient footage; a lower third under a cinematic title;
                // Bounce In mid-bounce beside a speech bubble.
                f = editor()
                p = f.project
                verify(p.importAndAppend(samples + "gradients_1080p30_h264.mp4"), "" + p.lastError)
                p.seek(0.5)
                tab(f, "Titles", "Video")
                template(f, "titles", "Big Bold")
                p.seek(1.5)
                shot("titles", dark)
                f.destroy(); wait(200)

                f = editor()
                p = f.project
                verify(p.importAndAppend(media + "night-street.mp4"), "" + p.lastError)
                p.seek(0)
                tab(f, "Titles", "Video")
                template(f, "titles", "Cinematic")
                p.selectedClip = 0
                p.seek(0)
                template(f, "lower", "Bar")
                p.seek(1.6)
                shot("titles-library", dark)
                f.destroy(); wait(200)

                f = editor()
                p = f.project
                verify(p.importAndAppend(media + "portrait-wide.mp4"), "" + p.lastError)
                p.seek(0)
                tab(f, "Titles", "Video")
                template(f, "callouts", "Speech Bubble")
                p.selectedClip = 0
                p.seek(0)
                template(f, "animated", "Bounce In")
                p.seek(0.35)
                shot("title-motion", dark)
                f.destroy(); wait(200)

                // Drawing on the player: a brush stroke, an arrow, a ring and a title on an arc.
                f = editor()
                p = f.project
                verify(p.importAndAppend(media + "night-street.mp4"), "" + p.lastError)
                p.seek(0.5)
                verify(p.addDrawing({ kind: "brush", stroke: [1, 0.84, 0.2, 1], width: 0.012,
                                      points: [[0.12, 0.7], [0.18, 0.62], [0.24, 0.6], [0.3, 0.64], [0.36, 0.7], [0.42, 0.66]] }) > 0, "" + p.lastError)
                verify(p.addDrawing({ kind: "arrow", stroke: [1, 1, 1, 1], width: 0.01, points: [[0.82, 0.18], [0.66, 0.36]] }) > 0, "" + p.lastError)
                verify(p.addDrawing({ kind: "ellipse", stroke: [1, 0.3, 0.3, 1], width: 0.01, points: [[0.55, 0.42], [0.72, 0.64]] }) > 0, "" + p.lastError)
                verify(p.addTitle({ text: "Mission ready", size: 0.08, bold: true }, -1, 0), "" + p.lastError)
                verify(p.setClipText(p.selectedClip, { curve: 0.6 }), "" + p.lastError)
                p.seek(1.5)
                findChild(f.editor, "drawButton").clicked()
                tryVerify(() => p.drawingMode)
                tab(f, "Media", "Video")
                shot("drawing", dark)
                findChild(f.editor, "drawButton").clicked()
                f.destroy(); wait(200)

                // An overlay laid over a cut: a warm light leak.
                f = editor()
                p = f.project
                verify(p.importAndAppend(media + "night-street.mp4"), "" + p.lastError)
                verify(p.importAndAppend(media + "portrait-wide.mp4"), "" + p.lastError)
                const join = main(p).clips[1].start
                p.seek(join)
                verify(p.addOverlayTransition("leak.warm"), "" + p.lastError)
                p.seek(join + 0.1)
                tab(f, "Transitions")
                chip(f, "transitionCategory:overlays")
                shot("overlays", dark)
                f.destroy(); wait(200)
            }
            EditingPreferences.reset()
            Theme.dark = true
        }
    }
}
