import QtQuick
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell
import "screens.js" as Screens

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
    Component { id: homeComponent; HomeScreen { anchors.fill: parent } }
    SignalSpy { id: finished; signalName: "analysisFinished" }
    readonly property string scratch: "file://" + screensMediaDir + "/"
    readonly property var warm: ({
        title: "Warm vignette",
        code: "vec4 effect(vec2 uv)\n{\n    vec4 c = texture(tve_source, uv);\n    float edge = smoothstep(0.25, 0.85, distance(uv, vec2(0.5)));\n    vec3 tinted = mix(c.rgb, c.rgb * tint * 1.25, amount);\n    return vec4(tinted * (1.0 - edge * amount), c.a);\n}\n",
        params: [{ id: "amount", title: "Amount", type: "float", min: 0, max: 1, default: 0.7 },
                 { id: "tint", title: "Tint", type: "color", default: [1.0, 0.78, 0.55] }]
    })
    TestCase {
        name: "Shot31d"
        when: windowShown
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
        function main(p) { return p.tracks.find((t) => t.main) }
        function street(f) {
            const p = f.project
            verify(p.importAndAppend(root.scratch + "night-street.mp4"))
            p.selectedClip = Number(main(p).clips[0].id)
            return p.selectedClip
        }
        function test_shots() {
            const window = root.Window.window
            for (const dark of [true, false]) {
                Theme.dark = dark
                EditingPreferences.reset()
                const tag = dark ? "dark" : "light"
                Screens.palette(root.Window.window, Theme)
                mouseMove(root, root.width - 4, root.height - 4)

                // A plane tracked: its corners on the player, the track listed.
                let f = createTemporaryObject(editorComponent, root)
                waitForRendering(f)
                let p = f.project
                finished.clear(); finished.target = p
                let clip = street(f)
                p.seek(0.5)
                findChild(f.editor, "inspectorTab:Analyze").clicked()
                wait(300)
                findChild(f.editor, "trackKind:plane").clicked()
                verify(p.setTrackArea(clip, { corners: [0.34, 0.30, 0.66, 0.33, 0.64, 0.68, 0.36, 0.71] }), p.lastError)
                verify(p.trackMotion(clip, { kind: "plane" }), p.lastError)
                tryVerify(() => finished.count >= 1, 120000)
                verify(finished.signalArguments[0][0], finished.signalArguments[0][1])
                verify(p.setTrackArea(clip, { corners: [0.34, 0.30, 0.66, 0.33, 0.64, 0.68, 0.36, 0.71] }))
                scrollTo(f, "trackKind:plane")
                wait(1500)
                grabImage(window.contentItem).save(screensDir + "/track-plane-" + tag + ".png")

                // A picture pinned onto it.
                compare(p.importMedia(["file://" + stillsDir + "/disc.png"]), 1)
                const video2 = p.tracks.find((t) => t.name === "Video 2")
                verify(p.addClip(p.media[1].id, video2.id, 0))
                const picture = p.selectedClip
                const tracks = p.tracksToFollow(picture)
                verify(tracks.length > 0)
                verify(p.followTrack(picture, tracks[0].clipId, tracks[0].trackId, { pin: true }), p.lastError)
                p.seek(1.5)
                mouseMove(root, root.width - 4, root.height - 4)
                findChild(f.editor, "inspectorTab:Analyze").clicked()
                wait(300)
                scrollTo(f, "followSection")
                const pin = findChild(f.editor, "followPin")
                if (pin) pin.checked = true
                wait(1500)
                grabImage(window.contentItem).save(screensDir + "/track-pin-" + tag + ".png")
                f.destroy(); wait(200)

                // Effects of your own: a shader effect on the clip, Mine in the library.
                f = createTemporaryObject(editorComponent, root)
                waitForRendering(f)
                p = f.project
                clip = street(f)
                const warmId = p.saveShaderEffect(root.warm)
                verify(warmId !== "", p.lastError)
                verify(p.addClipEffect(clip, "tve.color.black_white"))
                verify(p.addClipEffect(clip, "tve.stylize.vignette"))
                const stack = p.saveEffectStack(clip, "Moody night")
                verify(stack !== "", p.lastError)
                p.undo(); p.undo()
                verify(p.addClipEffect(clip, warmId))
                verify(p.setClipEffectParam(clip, 0, "amount", 1))
                p.seek(2)
                findChild(f.editor, "libraryTab:Effects").clicked()
                findChild(f.editor, "effectCategory:mine").clicked()
                findChild(f.editor, "inspectorTab:Effects").clicked()
                wait(1800)
                grabImage(window.contentItem).save(screensDir + "/my-effects-" + tag + ".png")

                // The shader editor, editing it.
                const editor = findChild(f.editor, "shaderEditor")
                editor.openFor(warmId)
                tryVerify(() => editor.previewId !== "", 8000)
                wait(1500)
                grabImage(window.contentItem).save(screensDir + "/shader-editor-" + tag + ".png")
                editor.close()
                wait(300)

                // Filters of your own: a grade and a LUT.
                verify(p.setClipColor(clip, { temperature: -0.4, saturation: -0.3, contrast: 0.2 }))
                verify(p.saveColorPreset(clip, "Blue hour"))
                verify(p.importLutAsFilter("file://" + fixturesDir + "/swap.cube", "Swapped channels"))
                findChild(f.editor, "libraryTab:Filters").clicked()
                findChild(f.editor, "lookCategory:mine").clicked()
                findChild(f.editor, "inspectorTab:Color").clicked()
                wait(1500)
                grabImage(window.contentItem).save(screensDir + "/my-filters-" + tag + ".png")

                // Stickers of your own: a picture, and a frame of the clip.
                verify(p.addMySticker("file://" + stillsDir + "/disc.png") !== "", p.lastError)
                p.selectedClip = clip
                verify(p.saveClipAsSticker(clip) !== "", p.lastError)
                mouseMove(root, root.width - 4, root.height - 4)
                findChild(f.editor, "libraryTab:Stickers").clicked()
                findChild(f.editor, "stickerCategory:mine").clicked()
                wait(1500)
                grabImage(window.contentItem).save(screensDir + "/my-stickers-" + tag + ".png")
                verify(p.removeMyEffect(warmId)); verify(p.removeMyEffect(stack))
                p.deleteColorPreset("Blue hour"); p.deleteColorPreset("Swapped channels")
                for (const s of p.stickers.filter((s) => s.category === "mine"))
                    p.removeMySticker(s.id)
                f.destroy(); wait(200)

                // The transport: go to the start and the end.
                f = createTemporaryObject(editorComponent, root)
                waitForRendering(f)
                p = f.project
                street(f)
                p.seek(1.6)
                p.selectedClip = 0
                mouseMove(root, root.width - 4, root.height - 4)
                wait(1500)
                grabImage(window.contentItem).save(screensDir + "/player-goto-" + tag + ".png")
                f.destroy(); wait(200)

                // Home in Japanese, the language menu open.
                Language.language = "ja"
                const home = createTemporaryObject(homeComponent, root)
                waitForRendering(home)
                const picker = findChild(home, "languagePicker")
                picker.popup.open()
                wait(1200)
                grabImage(window.contentItem).save(screensDir + "/languages-home-" + tag + ".png")
                picker.popup.close()
                home.destroy(); wait(150)

                // The editor in French.
                Language.language = "fr"
                f = createTemporaryObject(editorComponent, root)
                waitForRendering(f)
                p = f.project
                street(f)
                p.seek(2)
                findChild(f.editor, "inspectorTab:Color").clicked()
                wait(1500)
                grabImage(window.contentItem).save(screensDir + "/languages-editor-" + tag + ".png")
                f.destroy(); wait(200)
                Language.language = "en"
            }
            EditingPreferences.reset()
            Theme.dark = true
        }
    }
}
