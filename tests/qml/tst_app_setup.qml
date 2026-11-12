import QtQuick
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell

// Setting the app up as one likes it: shortcut presets, a key map file in and out, the
// keyboard drawn; favourites with tags in the library; workspaces; the full-screen preview.
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
        name: "AppSetup"
        when: windowShown

        function media(name) { return "file://" + analysisMediaDir + "/" + name }
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
            return item
        }
        function click(parent, name) {
            const item = settled(parent, name)
            mouseMove(item)
            mouseClick(item)
        }
        function openShortcuts(f) {
            click(f.editor, "shortcutsButton")
            const dialog = findChild(f.editor, "shortcutsDialog")
            tryVerify(() => dialog.opened)
            return dialog
        }
        function menuEntry(f, name) {
            const menu = findChild(f.editor, "workspaceMenu")
            for (let i = 0; menu && i < menu.count; ++i)
                if (menu.itemAt(i) && menu.itemAt(i).objectName === name)
                    return menu.itemAt(i)
            return null
        }
        function cleanup() {
            KeyMap.resetAll()
        }

        function test_shortcut_presets() {
            compare(KeyMap.preset, "techie")
            compare(KeyMap.presets.map((p) => p.id).join(","), "techie,premiere,finalcut")
            // No preset gives one key to two actions.
            for (const preset of KeyMap.presets) {
                verify(KeyMap.applyPreset(preset.id))
                compare(KeyMap.preset, preset.id)
                const keys = KeyMap.actions.reduce((all, a) => all.concat(a.sequences), [])
                compare(new Set(keys).size, keys.length, preset.id + " gives a key twice")
            }
            verify(KeyMap.applyPreset("premiere"))
            compare(KeyMap.sequences.split.join(), "Ctrl+K")
            compare(KeyMap.sequences.blade.join(), "C")
            compare(KeyMap.sequences.render.join(), "Return")
            compare(KeyMap.sequences.export.join(), "Ctrl+E") // (⌘M is the system's)
            verify(KeyMap.applyPreset("finalcut"))
            compare(KeyMap.sequences.split.join(), "Ctrl+B")
            compare(KeyMap.sequences.selectTool.join(), "A")
            compare(KeyMap.sequences.rippleDelete.join(","), "Backspace,Del")
            compare(KeyMap.sequences.delete.join(","), "Shift+Backspace,Shift+Del")
            verify(!KeyMap.applyPreset("nope"))

            // In the editor: Premiere's ⌘K splits at the playhead.
            verify(KeyMap.applyPreset("premiere"))
            const f = createEditor()
            const p = f.project
            verify(p.importAndAppend(media("scenes.mov")))
            p.seek(1.5)
            mouseClick(settled(f.editor, "player")) // (the editor has the keys)
            keyClick(Qt.Key_K, Qt.ControlModifier)
            tryCompare(p.tracks.find((t) => t.main).clips, "length", 2)

            // The dialog names the preset, and "Custom" once a key is changed.
            const dialog = openShortcuts(f)
            const picker = findChild(dialog.contentItem, "shortcutPreset")
            compare(picker.displayText, "Like Premiere Pro")
            verify(findChild(dialog.contentItem, "shortcutPresetDescription").text.indexOf("⌘K splits") === 0)
            verify(KeyMap.assign("group", "Ctrl+Alt+Shift+G"))
            compare(KeyMap.preset, "")
            compare(picker.displayText, "Custom")
            picker.activated(2) // Final Cut Pro
            compare(KeyMap.preset, "finalcut")
            compare(picker.displayText, "Like Final Cut Pro")
            dialog.close()
        }

        function test_shortcuts_out_and_back() {
            const file = scratchDir + "/shortcuts/mine.json"
            verify(testFiles.write(file, "")) // (the folder)
            verify(KeyMap.applyPreset("finalcut"))
            verify(KeyMap.assign("group", "Ctrl+Alt+Shift+G"))
            verify(KeyMap.exportTo("file://" + file))
            const written = JSON.parse(testFiles.read(file))
            compare(written.app, "TechieVideoEditor")
            compare(Object.keys(written.bindings).length, KeyMap.actions.length) // every action
            compare(written.bindings.split.join(), "Ctrl+B")

            KeyMap.resetAll()
            compare(KeyMap.preset, "techie")
            compare(KeyMap.importFrom("file://" + file), KeyMap.actions.length)
            compare(KeyMap.sequences.split.join(), "Ctrl+B")
            compare(KeyMap.sequences.group.join(), "Ctrl+Alt+Shift+G")
            compare(KeyMap.preset, "")

            // Keys the system keeps, actions it does not know and a key named twice are left
            // out; actions not named take their defaults.
            const odd = scratchDir + "/shortcuts/odd.json"
            verify(testFiles.write(odd, JSON.stringify({ version: 1, bindings: {
                split: ["Ctrl+Q", "Ctrl+K"], nope: ["X"], copy: ["Ctrl+K"] } })))
            const f = createEditor()
            const dialog = openShortcuts(f)
            compare(dialog.importFile("file://" + odd), 2)
            compare(findChild(dialog.contentItem, "shortcutsNotice").text, "Imported the keys of 2 actions.")
            compare(KeyMap.sequences.split.join(), "Ctrl+K")
            compare(KeyMap.sequences.copy.length, 0)
            compare(KeyMap.sequences.group.join(), "Ctrl+G")
            // Not a shortcuts file.
            verify(testFiles.write(scratchDir + "/shortcuts/other.json", "{\"hello\": 1}"))
            compare(KeyMap.importFrom("file://" + scratchDir + "/shortcuts/other.json"), -1)
            compare(KeyMap.lastError, "other.json is not a TechieVideoEditor shortcuts file")
            dialog.close()
        }

        function test_the_keyboard_drawn() {
            const f = createEditor()
            const dialog = openShortcuts(f)
            const content = dialog.contentItem
            click(content, "shortcutsKeyboard")
            const map = settled(content, "keyboardMap")
            verify(!findChild(content, "shortcutSearch").visible)
            // No modifiers: S splits, Q does nothing.
            const s = findChild(map, "keyboardKey:S")
            verify(s.inUse)
            compare(s.actions[0].id, "split")
            verify(!findChild(map, "keyboardKey:Q").inUse)
            verify(findChild(map, "keyboardKey:Space").inUse)
            // With ⌘: S saves, G groups.
            click(map, "keyboardModifier:Ctrl")
            compare(map.modifiers.join(), "Ctrl")
            compare(findChild(map, "keyboardKey:S").actions[0].id, "save")
            compare(findChild(map, "keyboardKey:G").actions[0].id, "group")
            verify(findChild(map, "keyboardUsed").text.indexOf("shortcuts on these keys") > 0)
            // ⌘⇧: G ungroups.
            click(map, "keyboardModifier:Shift")
            compare(findChild(map, "keyboardKey:G").actions[0].id, "ungroup")
            verify(!findChild(map, "keyboardKey:S").inUse)
            // Clicking a key in use waits for its new keys.
            click(map, "keyboardKey:G")
            compare(dialog.recording, "ungroup")
            dialog.pressed(Qt.Key_U, Qt.ControlModifier | Qt.ShiftModifier)
            compare(KeyMap.sequences.ungroup.join(), "Ctrl+Shift+U")
            verify(findChild(map, "keyboardKey:U").inUse)
            verify(!findChild(map, "keyboardKey:G").inUse)
            dialog.close()
        }

        function test_favourites_with_tags() {
            const f = createEditor()
            const library = f.editor.libraryView
            library.currentTab = library.tabs.findIndex((t) => t.key === "Effects")
            const grid = settled(library, "effectsGrid")
            const effects = f.editor.player.availableEffects
            verify(effects.length > 3)
            const first = effects[0], second = effects[1]

            // ☆ on a tile shows while it is hovered; clicked, it is a favourite.
            const tile = settled(library, "libraryEffect:" + first.id)
            mouseMove(tile, tile.width / 2, tile.height / 2)
            const star = settled(library, "favourite:effect:" + first.id)
            mouseMove(star)
            mouseClick(star)
            verify(Favourites.isFavourite("effect", first.id))
            verify(Favourites.toggle("effect", second.id))
            // ★ Favourites shows only them.
            click(library, "effectCategory:favourites")
            tryCompare(grid, "count", 2)

            // Tags: typed in the editor (any case, commas between), one of them on the other too.
            click(library, "favouriteTags:effect:" + first.id)
            const editor = findChild(library, "favouriteTagEditor")
            tryVerify(() => editor.opened)
            const field = findChild(editor.contentItem, "favouriteTagField")
            field.text = "Wedding,  soft "
            field.accepted()
            compare(Favourites.tagsOf("effect", first.id).join(), "wedding,soft")
            verify(findChild(editor.contentItem, "favouriteTagRemove:wedding"))
            editor.close()
            Favourites.setTags("effect", second.id, ["soft"])
            compare(Favourites.tagsFor("effect").join(), "soft,wedding")
            // By tag.
            click(library, "favouriteTag:effect:wedding")
            tryCompare(grid, "count", 1)
            click(library, "favouriteTag:effect:soft")
            tryCompare(grid, "count", 2)
            click(library, "favouriteTag:effect:all")
            // The search finds a tag too, among every effect.
            click(library, "effectCategory:all")
            library.searchText = "weddi"
            tryCompare(grid, "count", 1)
            library.searchText = ""

            // Other kinds keep their own: a look and a title.
            verify(Favourites.toggle("look", f.project.looks[0].id))
            library.currentTab = library.tabs.findIndex((t) => t.key === "Filters")
            click(library, "lookCategory:favourites")
            tryCompare(settled(library, "looksGrid"), "count", 1)
            verify(Favourites.toggle("title", "Big Bold"))
            library.currentTab = library.tabs.findIndex((t) => t.key === "Titles")
            click(library, "titleCategory:favourites")
            tryCompare(settled(library, "titlesGrid"), "count", 1)

            // Kept in the favourites file; taking the star off takes the tags.
            const saved = JSON.parse(testFiles.read(favouritesFile)).favourites
            compare(saved["effect:" + first.id].join(), "wedding,soft")
            verify(saved["title:Big Bold"] !== undefined)
            verify(!Favourites.toggle("effect", first.id))
            compare(Favourites.tagsOf("effect", first.id).length, 0)
            for (const key of Object.keys(Favourites.items)) {
                const at = key.indexOf(":")
                Favourites.toggle(key.substring(0, at), key.substring(at + 1))
            }
        }

        function test_workspaces() {
            compare(Workspaces.current, "editing")
            compare(Workspaces.layouts.map((w) => w.id).join(","), "editing,organising,colour,audio,preview")
            let f = createEditor()
            const library = f.editor.libraryView, inspector = f.editor.inspectorView, timeline = f.editor.timelineView
            verify(library.visible && inspector.visible)

            // Colour, from the menu: no library, a wide inspector, a shorter timeline.
            click(f.editor, "workspaceButton")
            const item = menuEntry(f, "workspace:colour")
            verify(item)
            item.triggered()
            findChild(f.editor, "workspaceMenu").dismiss()
            compare(Workspaces.current, "colour")
            tryVerify(() => !library.visible && Math.abs(inspector.width - 460) < 2, 3000, "inspector " + inspector.width)
            tryVerify(() => timeline.height < 0.33 * (timeline.height + f.editor.player.height + 200), 3000)
            // Big preview: neither side panel.
            verify(f.editor.applyWorkspace("preview"))
            tryVerify(() => !library.visible && !inspector.visible)
            // Audio: both back, the timeline tall.
            verify(f.editor.applyWorkspace("audio"))
            tryVerify(() => library.visible && inspector.visible && timeline.height > f.editor.player.height)

            // Saved as it is, by name; the same name replaces it.
            verify(f.editor.applyWorkspace("colour"))
            tryVerify(() => !library.visible && Math.abs(inspector.width - 460) < 2) // (laid out before it is saved)
            click(f.editor, "workspaceButton")
            menuEntry(f, "workspaceSave").triggered()
            findChild(f.editor, "workspaceMenu").dismiss()
            const dialog = findChild(f.editor, "workspaceNameDialog")
            tryVerify(() => dialog.opened)
            findChild(dialog.contentItem, "workspaceName").text = "Grading"
            click(dialog.contentItem, "workspaceSaveButton")
            tryVerify(() => !dialog.visible)
            const mine = Workspaces.layout(Workspaces.current)
            compare(mine.name, "Grading")
            compare(mine.builtIn, false)
            compare(mine.library, 0)
            verify(Math.abs(mine.inspector - 460) < 2, JSON.stringify(mine) + " inspector " + f.editor.inspectorView.width)
            compare(f.editor.saveWorkspace("grading"), mine.id) // replaced, not another
            compare(Workspaces.layouts.length, 6)
            verify(JSON.parse(testFiles.read(workspacesFile)).custom[0].name === "grading")

            // A new editor opens in it.
            f.destroy()
            f = createEditor()
            tryVerify(() => !f.editor.libraryView.visible, 3000, "the workspace did not come back")
            // Deleted: back to Editing.
            verify(Workspaces.remove(mine.id))
            verify(!Workspaces.remove("editing"))
            compare(Workspaces.current, "editing")
            verify(f.editor.applyWorkspace("editing"))
            tryVerify(() => f.editor.libraryView.visible && f.editor.inspectorView.visible)
        }

        function test_full_screen_preview() {
            const f = createEditor()
            const p = f.project
            verify(p.importAndAppend(media("scenes.mov")))
            click(f.editor, "secondDisplayButton")
            let preview = null
            tryVerify(() => {
                preview = f.editor.secondDisplayPreview || null
                return preview !== null && preview.visible
            }, 5000, "the preview never opened")
            verify(preview.screen)
            compare(preview.onAnotherDisplay, Qt.application.screens.length > 1)
            const view = findChild(preview.contentItem, "secondDisplayView")
            compare(view.follows, f.editor.player)
            // The player's frames, by its clock; Space plays and pauses from the preview.
            findChild(preview, "secondDisplayKey:play").activated()
            verify(p.playing)
            tryVerify(() => view.presentFps > 5, 8000, "the preview never showed the playing frames")
            findChild(preview, "secondDisplayKey:play").activated()
            verify(!p.playing)
            // Esc closes it.
            findChild(preview, "secondDisplayKey:close").activated()
            tryVerify(() => !f.editor.secondDisplayPreview, 3000, "the preview stayed open")
            verify(!findChild(f.editor, "secondDisplayButton").checked)
        }
    }
}
