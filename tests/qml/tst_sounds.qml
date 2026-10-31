import QtQuick
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell

// The Audio tab through the real editor: our own music and sound effects, previews, adding
// at the playhead, My music folders and sound packs (with a tampered one; fixtures written by
// main.cpp, into scratch folders set by TVE_SOUND_* variables).
Item {
    id: root
    width: 1400
    height: 900

    Component {
        id: editorComponent
        Item {
            property alias project: controller
            property alias editor: screen
            anchors.fill: parent
            ProjectController { id: controller; view: screen.player }
            EditorScreen { id: screen; anchors.fill: parent; project: controller }
        }
    }

    TestCase {
        id: testCase
        name: "Sounds"
        when: windowShown

        function createEditor() {
            const fixture = createTemporaryObject(editorComponent, root)
            verify(fixture)
            waitForRendering(fixture)
            return fixture
        }
        function settled(parent, name) {
            let item = null
            let last = null
            tryVerify(() => {
                item = findChild(parent, name)
                if (!item || !item.visible)
                    return false
                const p = item.mapToItem(root, 0, 0)
                const stable = last !== null && p.x === last.x && p.y === last.y
                last = p
                return stable
            }, 3000, name + " never settled")
            for (let a = item; a && a !== root; a = a.parent)
                waitForPolish(a, 1000)
            return item
        }
        function click(item) {
            mouseMove(item)
            mouseClick(item)
        }
        // A sound's control, its row scrolled into view first.
        function soundControl(page, name) {
            const id = name.substring(name.indexOf(":") + 1)
            const index = page.shown.findIndex((s) => s.id === id)
            if (index >= 0)
                findChild(page, "soundList").positionViewAtIndex(index, ListView.Contain)
            return settled(page, name)
        }
        function openAudioTab(f) {
            click(settled(f.editor, "libraryTab:Audio"))
            return settled(f.editor, "audioPage")
        }
        function clipById(project, id) {
            for (const track of project.tracks) {
                const clip = track.clips.find((c) => c.id === id)
                if (clip)
                    return { track: track, clip: clip }
            }
            return null
        }
        function cleanUp(f) {
            const library = f.project.soundLibrary
            for (const folder of library.musicFolders.slice())
                library.removeMusicFolder(folder)
        }

        function test_music_and_effects_by_category() {
            const f = createEditor()
            const page = openAudioTab(f)
            const library = f.project.soundLibrary
            // (Packs another test installed come after the built-in sounds.)
            const builtIn = library.sounds.filter((s) => s.pack === "")
            compare(builtIn.filter((s) => s.kind === "music").length, 12)
            compare(builtIn.filter((s) => s.kind === "sfx").length, 15)
            for (const sound of builtIn)
                verify(sound.duration > 0, sound.id)
            compare(page.shown.length, library.sounds.filter((s) => s.kind === "music").length)
            click(settled(page, "audioCategory:calm"))
            compare(page.shown.length, 2)
            verify(page.shown.every((s) => s.category === "calm"))
            click(settled(page, "audioCategory:all"))
            click(settled(page, "audioSection:sfx"))
            compare(page.category, "")
            compare(page.shown.length, library.sounds.filter((s) => s.kind === "sfx").length)
            verify(settled(page, "audioCategory:interface"))
            // The library's search narrows the list.
            findChild(f.editor, "librarySearch").text = "whoosh"
            compare(page.shown.length, 1)
            compare(page.shown[0].id, "tve.sfx.whoosh")
            verify(soundControl(page, "sound:tve.sfx.whoosh"))
        }

        function test_preview_plays_and_stops() {
            const f = createEditor()
            const page = openAudioTab(f)
            const library = f.project.soundLibrary
            click(settled(page, "audioSection:sfx"))
            const play = soundControl(page, "previewSound:tve.sfx.chime")
            click(play)
            compare(library.previewing, "tve.sfx.chime")
            compare(play.iconName, "pause")
            // A short effect plays through and stops by itself.
            tryVerify(() => library.previewProgress > 0.2, 3000)
            tryCompare(library, "previewing", "", 5000)
            compare(play.iconName, "play")
            // Music previews until stopped (the same button), or until another one starts.
            click(settled(page, "audioSection:music"))
            const first = page.shown[0].id
            click(soundControl(page, "previewSound:" + first))
            compare(library.previewing, first)
            click(soundControl(page, "previewSound:" + page.shown[1].id))
            compare(library.previewing, page.shown[1].id)
            click(soundControl(page, "previewSound:" + page.shown[1].id))
            compare(library.previewing, "")
            // Leaving the tab stops a preview.
            click(soundControl(page, "previewSound:" + first))
            compare(library.previewing, first)
            click(settled(f.editor, "libraryTab:Media"))
            compare(library.previewing, "")
        }

        function test_add_music_and_effects_at_the_playhead() {
            const f = createEditor()
            const p = f.project
            const page = openAudioTab(f)
            const music = page.shown[0]
            // (A picture to seek within: the playhead stays inside the timeline.)
            compare(p.importMedia(["file://" + analysisMediaDir + "/scenes.mov"]), 1)
            verify(p.addClip(p.media[0].id))
            p.seek(1.0)
            click(soundControl(page, "addSound:" + music.id))
            compare(p.undoText, "Add Music")
            const added = clipById(p, p.selectedClip)
            verify(added)
            compare(added.track.kind, "audio")
            compare(added.clip.start, 1.0)
            verify(Math.abs(added.clip.duration - music.duration) < 0.05, added.clip.duration)
            // Rendered once into the cache, as a WAV.
            verify(added.clip.path.indexOf(soundCacheDir) === 0, added.clip.path)
            verify(added.clip.path.endsWith(".wav"))
            const media = p.media.find((m) => m.id === added.clip.mediaId)
            verify(media.hasAudio)
            verify(!media.hasVideo)

            // An effect over the music goes on a new Sound effects track; the same effect
            // again uses the same media.
            click(settled(page, "audioSection:sfx"))
            p.seek(2.0)
            click(soundControl(page, "addSound:tve.sfx.pop"))
            compare(p.undoText, "Add Sound Effect")
            const pop = clipById(p, p.selectedClip)
            compare(pop.track.name, "Sound effects")
            compare(pop.clip.start, 2.0)
            const mediaCount = p.media.length
            p.seek(5.0)
            mouseDoubleClickSequence(soundControl(page, "sound:tve.sfx.pop"), 120, 10)
            const again = clipById(p, p.selectedClip)
            compare(again.clip.start, 5.0)
            compare(again.clip.mediaId, pop.clip.mediaId)
            compare(p.media.length, mediaCount)
            // Undo takes the clip away but keeps media other clips use.
            p.undo()
            verify(!clipById(p, again.clip.id))
            compare(p.media.length, mediaCount)
            p.undo()
            verify(!clipById(p, pop.clip.id))
            verify(!p.tracks.find((t) => t.name === "Sound effects"))
            verify(!p.media.find((m) => m.id === pop.clip.mediaId))
        }

        function test_my_music_from_folders() {
            const f = createEditor()
            const p = f.project
            const page = openAudioTab(f)
            const library = p.soundLibrary
            click(settled(page, "audioSection:mine"))
            compare(page.shown.length, 0)
            verify(!library.addMusicFolder("file://" + soundFixturesDir + "/missing"))
            verify(library.addMusicFolder("file://" + soundFixturesDir + "/music"))
            compare(library.musicFolders, [soundFixturesDir + "/music"])
            // Sub-folders too, by name.
            compare(page.shown.map((s) => s.title), ["Evening tune", "Morning tune"])
            verify(Math.abs(page.shown[0].duration - 3) < 0.05)
            click(soundControl(page, "previewSound:" + page.shown[1].id))
            compare(library.previewing, page.shown[1].id)
            click(soundControl(page, "addSound:" + page.shown[0].id))
            const added = clipById(p, p.selectedClip)
            compare(added.clip.path, soundFixturesDir + "/music/Evening tune.wav")
            compare(added.track.kind, "audio")

            // The folders are remembered (a new editor reads them back).
            const g = createEditor()
            compare(g.project.soundLibrary.musicFolders, [soundFixturesDir + "/music"])
            compare(g.project.soundLibrary.myMusic.length, 2)
            g.visible = false
            click(settled(page, "removeMusicFolder:" + soundFixturesDir + "/music"))
            compare(library.musicFolders, [])
            compare(page.shown.length, 0)
            cleanUp(g)
        }

        function test_sound_packs_are_checked() {
            const f = createEditor()
            const p = f.project
            const page = openAudioTab(f)
            const library = p.soundLibrary
            verify(!library.importPack("file://" + soundFixturesDir + "/music"))
            verify(library.lastError().length > 0)
            verify(library.importPack("file://" + soundFixturesDir + "/packs/good"))
            let packSounds = library.sounds.filter((s) => s.pack !== "")
            compare(packSounds.map((s) => s.id).sort(), ["test.good/tve.sfx.ding", "test.good/tve.sfx.pop"])
            compare(packSounds[0].pack, "Test pack")
            compare(library.packProblems, [])
            // A file changed after the manifest was written is left out, and said so.
            verify(library.importPack("file://" + soundFixturesDir + "/packs/tampered"))
            packSounds = library.sounds.filter((s) => s.pack === "Tampered pack")
            compare(packSounds.map((s) => s.id), ["test.tampered/tve.sfx.pop"])
            compare(library.packProblems.length, 1)
            verify(library.packProblems[0].indexOf("Ding") >= 0, library.packProblems[0])
            click(settled(page, "audioSection:sfx"))
            verify(settled(page, "packProblems"))
            // A pack's sound previews and goes on the timeline from the pack's own file.
            click(soundControl(page, "previewSound:test.good/tve.sfx.ding"))
            compare(library.previewing, "test.good/tve.sfx.ding")
            click(soundControl(page, "addSound:test.good/tve.sfx.ding"))
            const added = clipById(p, p.selectedClip)
            verify(added.clip.path.indexOf(soundPacksDir) === 0, added.clip.path)
            verify(added.clip.path.endsWith("/sfx/ding.wav"))
        }
    }
}
