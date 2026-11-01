import QtQuick
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell

// The pack store through the real editor, against a pack site on this machine (packSite,
// main.cpp): packs published with the real publishing code and a catalogue signed with a key
// made for this run. Installing sounds, stickers and templates, which then show in their
// tabs; an update that downloads only what changed; removing; and refusing what can't be
// trusted — a catalogue signed by another key or older than one seen, a pack file or
// manifest that isn't what was published, a plain-http address elsewhere.
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
        name: "Store"
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
        function press(item) {
            verify(item.enabled, item.objectName + " is disabled")
            item.clicked()
        }
        function pack(store, id) { return store.packs.find((p) => p.id === id) }
        // What the store says when it finishes the next thing.
        function finished(store) {
            let result = null
            store.finished.connect((action, id, ok, message) => { if (!result) result = { action: action, id: id, ok: ok, message: message } })
            return () => result
        }
        function publishSounds(version, extra) {
            const files = { "sounds": soundFixturesDir + "/packs/good" }
            if (extra)
                files["sounds/extra.txt"] = packSite.text("extra.txt", "more")
            compare(packSite.publish(files, "test.sounds", version, "Test sounds", "Two little sounds"), "")
        }
        function publishStickers() {
            const list = packSite.text("stickers.json", JSON.stringify({ stickers: [
                { id: "dot", title: "Pack dot", category: "shapes", file: "dot.json" }] }))
            compare(packSite.publish({ "stickers/stickers.json": list, "stickers/dot.json": ":/effects/stickers/star_pulse.json" },
                                     "test.stickers", 1, "Test stickers"), "")
        }

        function init() {
            packSite.reset()
        }

        function test_install_update_and_remove() {
            publishSounds(1, false)
            publishStickers()
            packSite.sign()
            const f = createEditor()
            const p = f.project
            const store = p.store
            verify(store.hasTrustedKey)
            compare(store.catalogueUrl, packSite.url)

            // A template of our own, published as a pack (and then not ours any more).
            compare(p.importMedia(["file://" + stillsDir + "/halves.png"]), 1)
            verify(p.addClip(p.media[0].id))
            const own = p.saveAsTemplate("Trip", "travel", [p.tracks.find((t) => t.main).clips[0].id], [], -1)
            verify(own !== "", p.lastError)
            compare(packSite.publish({ "templates/Trip.tvet": templatesDir + "/Trip.tvet" }, "test.templates", 1, "Test templates",
                                     "A trip in one shot", stillsDir + "/halves.png"), "")
            verify(p.templates.remove(own))
            packSite.sign()

            // Opening the Store reads the catalogue.
            const rail = findChild(f.editor, "libraryTabRail")
            rail.contentY = rail.contentHeight - rail.height
            press(settled(f.editor, "libraryTab:Store"))
            settled(f.editor, "storePage")
            tryVerify(() => store.checked, 10000, store.lastError)
            compare(store.packs.length, 3)
            verify(store.packs.every((x) => x.state === "available"))
            compare(pack(store, "test.templates").contents, ["templates"])
            tryVerify(() => pack(store, "test.templates").preview !== "", 5000, "the preview never came")
            verify(settled(f.editor, "storeStatus").text.indexOf("Catalogue of 2026-11-01") === 0)

            // Sounds: into the Audio tab, and onto the timeline.
            let done = finished(store)
            press(settled(f.editor, "packInstall:test.sounds"))
            tryVerify(() => done() !== null, 10000)
            verify(done().ok, done().message)
            compare(done().message, "Installed Test sounds")
            compare(pack(store, "test.sounds").state, "installed")
            const sound = p.soundLibrary.sounds.find((s) => s.pack === "Test pack")
            verify(sound)
            verify(p.addSound(sound.id))

            // Stickers and the template.
            done = finished(store)
            press(settled(f.editor, "packInstall:test.stickers"))
            tryVerify(() => done() !== null, 10000)
            verify(done().ok, done().message)
            const sticker = p.stickers.find((s) => s.id === "pack.test.stickers/dot")
            verify(sticker)
            compare(sticker.pack, "Test stickers")
            verify(p.addSticker(sticker.id))
            done = finished(store)
            verify(store.install("test.templates"))
            tryVerify(() => done() !== null, 10000)
            verify(done().ok, done().message)
            const template = p.templates.templates.find((t) => t.id === "pack.test.templates/Trip")
            verify(template)
            compare(template.pack, "Test templates")
            compare(template.slots, 1)
            verify(!p.templates.remove(template.id))
            verify(!store.restartNeeded)

            // An update: only what changed is downloaded (the manifest and the new file).
            publishSounds(2, true)
            packSite.sign()
            done = finished(store)
            verify(store.refresh())
            tryVerify(() => done() !== null, 10000)
            compare(pack(store, "test.sounds").state, "update")
            const before = packSite.requests
            done = finished(store)
            press(settled(f.editor, "packInstall:test.sounds"))
            tryVerify(() => done() !== null, 10000)
            compare(done().message, "Updated Test sounds")
            compare(packSite.requests - before, 2)
            compare(pack(store, "test.sounds").installedVersion, 2)
            verify(testFiles.exists(packSite.packsDir + "/test.sounds/sounds/extra.txt"))

            // Removing: gone from the folder and the Stickers tab.
            press(settled(f.editor, "packRemove:test.stickers"))
            verify(!testFiles.exists(packSite.packsDir + "/test.stickers"))
            verify(!p.stickers.some((s) => s.id === "pack.test.stickers/dot"))
            compare(pack(store, "test.stickers").state, "available")
            {
                findChild(f.editor, "storeFilter:installed").clicked()
                tryVerify(() => !findChild(f.editor, "pack:test.stickers"))
                verify(findChild(f.editor, "pack:test.sounds"))
            }

            // Effects change at the next start.
            compare(packSite.publish({ "effects/fill_copy": ":/effects/video/background_fill" }, "test.effects", 1, "Test effects"), "")
            packSite.sign()
            done = finished(store)
            verify(store.refresh())
            tryVerify(() => done() !== null, 10000)
            done = finished(store)
            verify(store.install("test.effects"))
            tryVerify(() => done() !== null, 10000)
            verify(done().ok, done().message)
            verify(store.restartNeeded)
            verify(settled(f.editor, "storeRestart"))
        }

        function test_refuses_what_it_cannot_trust() {
            publishStickers()
            // Signed by a key it doesn't trust.
            packSite.sign(5, false)
            const f = createEditor()
            const store = f.project.store
            let done = finished(store)
            verify(store.refresh())
            tryVerify(() => done() !== null, 10000)
            verify(!done().ok)
            verify(done().message.indexOf("signature isn't one we trust") >= 0, done().message)
            verify(!store.checked)

            // Signed properly; then an older one (a replay) is refused.
            packSite.sign(5)
            done = finished(store)
            verify(store.refresh())
            tryVerify(() => done() !== null, 10000)
            verify(done().ok, done().message)
            packSite.sign(3)
            done = finished(store)
            verify(store.refresh())
            tryVerify(() => done() !== null, 10000)
            verify(done().message.indexOf("older than one seen before") >= 0, done().message)

            // A pack file changed after publishing: nothing installed, nothing left behind.
            packSite.sign(6)
            done = finished(store)
            verify(store.refresh())
            tryVerify(() => done() !== null, 10000)
            packSite.tamper("test.stickers/1/stickers/dot.json")
            done = finished(store)
            verify(store.install("test.stickers"))
            tryVerify(() => done() !== null, 10000)
            verify(!done().ok)
            compare(done().message, "stickers/dot.json is not the file the pack lists")
            compare(pack(store, "test.stickers").state, "available")
            verify(!testFiles.exists(packSite.packsDir + "/test.stickers"))
            verify(!testFiles.exists(packSite.packsDir + "/.staging/test.stickers"))
            // Its manifest changed: refused before any file is fetched.
            packSite.tamper("test.stickers/1/pack.json")
            done = finished(store)
            verify(store.install("test.stickers"))
            tryVerify(() => done() !== null, 10000)
            compare(done().message, "The pack's manifest is not the one the catalogue lists")
            // Failing before anything was staged deletes nothing outside the packs folder.
            verify(testFiles.exists(packSite.currentDir))

            // Plain http elsewhere is not used; the default comes back.
            store.catalogueUrl = "http://example.com/catalogue.json"
            verify(!store.refresh())
            verify(store.lastError.indexOf("https://") >= 0)
            store.catalogueUrl = ""
            compare(store.catalogueUrl, packSite.url)
        }
    }
}
