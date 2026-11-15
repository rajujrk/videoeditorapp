import QtQuick
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell

// Recent projects on the Home screen: projects saved and opened are listed newest first,
// with a cover, kept for the next start; a file moved away shows as missing; one can be taken
// off the list; a card opens its project; the Projects page searches and sorts them all.
// Each test keeps its list in a scratch file of its own (TVE_RECENT_PROJECTS_FILE).
Item {
    id: root
    width: 1400
    height: 780

    Component { id: controllerComponent; ProjectController {} }
    Component { id: homeComponent; HomeScreen { anchors.fill: parent } }
    Component { id: spyComponent; SignalSpy {} }

    TestCase {
        id: testCase
        name: "RecentProjects"
        when: windowShown

        readonly property string av: "file://" + samplesDir + "/test_av_1080p30.mp4"
        property string folder

        function init() {
            folder = scratchDir + "/recent-" + Date.now() + "-" + Math.floor(Math.random() * 1e6)
            testFiles.setEnv("TVE_RECENT_PROJECTS_FILE", folder + "/settings/recent-projects.json")
            verify(testFiles.write(folder + "/.made", "")) // (the folder the projects are saved in)
        }

        function controller() {
            const c = createTemporaryObject(controllerComponent, testCase)
            verify(c)
            return c
        }
        // A project with the test clip in it, saved as <folder>/<name>.tve; its path.
        function saved(c, name, width, height) {
            c.newProject(width || 1920, height || 1080)
            verify(c.importAndAppend(av))
            const path = folder + "/" + name + ".tve"
            verify(c.save("file://" + path), c.lastError)
            return path
        }
        function paths(c) {
            return c.recentProjects.map((p) => p.path)
        }
        // The Home screen for `c`, once every cover is in (a cover arriving makes the cards again).
        function homeWith(c) {
            tryVerify(() => c.recentProjects.every((p) => p.missing || p.duration === 0 || p.cover !== ""), 20000, "covers")
            const home = createTemporaryObject(homeComponent, root, { project: c })
            verify(home)
            waitForRendering(home)
            return home
        }
        // A project's card on the page shown (Home: "recentProject:", Projects: "projectCard:").
        function card(home, path) {
            const name = (home.page === "projects" ? "projectCard:" : "recentProject:") + path
            let found = null
            tryVerify(() => (found = findChild(home, name)) !== null && found.visible
                            && found.width > 0, 3000, "no card for " + path)
            // Placed by its Flow (new cards start out on top of each other).
            tryVerify(() => {
                const centre = found.mapToItem(found.parent, found.width / 2, found.height / 2)
                return found.parent.childAt(centre.x, centre.y) === found
            }, 3000, "card not placed")
            waitForRendering(found)
            return found
        }

        function test_saved_projects_are_listed_newest_first() {
            const c = controller()
            compare(c.recentProjects.length, 0)
            const alpha = saved(c, "Alpha")
            const beta = saved(c, "Beta", 1080, 1920)
            compare(paths(c), [beta, alpha])
            const first = c.recentProjects[0]
            compare(first.name, "Beta")
            compare(first.width, 1080)
            compare(first.height, 1920)
            verify(first.duration > 1, "duration " + first.duration)
            verify(first.lastOpened > Date.now() - 60000)
            compare(first.missing, false)
            compare(first.folder, folder)
            // Saved again: to the top, once.
            c.open("file://" + alpha)
            verify(c.save())
            compare(paths(c), [alpha, beta])
            // A cover, rendered a moment later.
            tryVerify(() => c.recentProjects[0].cover !== "", 10000, "no cover")
            verify(c.recentProjects[0].cover.startsWith("file://"))
        }

        function test_opened_projects_are_listed_and_kept_for_next_time() {
            const a = controller()
            const alpha = saved(a, "Alpha")
            const beta = saved(a, "Beta")
            // Another start of the app: the list is still there; opening Alpha moves it up.
            const b = controller()
            compare(paths(b), [beta, alpha])
            verify(b.open("file://" + alpha))
            compare(paths(b), [alpha, beta])
            const c = controller()
            compare(paths(c), [alpha, beta])
            // Opened as Finder does (app/Main.qml's openFile calls open too).
            verify(c.open(Qt.resolvedUrl("file://" + beta)))
            compare(paths(c)[0], beta)
            // The file itself.
            const stored = JSON.parse(testFiles.read(folder + "/settings/recent-projects.json"))
            compare(stored.version, 1)
            compare(stored.projects.map((p) => p.path), [beta, alpha])
        }

        function test_a_moved_file_shows_as_missing() {
            const c = controller()
            const alpha = saved(c, "Alpha")
            const beta = saved(c, "Beta")
            const home = homeWith(c)
            verify(testFiles.move(alpha, folder + "/moved/Alpha.tve"))
            c.refreshRecentProjects()
            compare(c.recentProjects.length, 2) // not dropped
            compare(c.recentProjects[1].path, alpha)
            compare(c.recentProjects[1].missing, true)
            compare(c.recentProjects[0].missing, false)
            const missing = card(home, alpha)
            verify(findChild(missing, "recentMissing").visible)
            verify(!findChild(card(home, beta), "recentMissing").visible)
            // Clicking it opens nothing; asked to, it says why.
            const spy = createTemporaryObject(spyComponent, testCase, { target: home, signalName: "recentRequested" })
            mouseClick(missing)
            compare(spy.count, 0)
            verify(!c.openRecentProject(alpha))
            verify(c.lastError.indexOf("Alpha.tve") >= 0, c.lastError)
            verify(!c.revealRecentProject(alpha))
            compare(c.recentProjects.length, 2)
            // Back where it was: no longer missing.
            verify(testFiles.move(folder + "/moved/Alpha.tve", alpha))
            c.refreshRecentProjects()
            compare(c.recentProjects[1].missing, false)
            tryVerify(() => !findChild(card(home, alpha), "recentMissing").visible)
        }

        function test_removed_from_the_list_from_its_menu() {
            const c = controller()
            const alpha = saved(c, "Alpha")
            const beta = saved(c, "Beta")
            const home = homeWith(c)
            const target = card(home, alpha)
            mouseClick(target, target.width / 2, target.height / 2, Qt.RightButton)
            const menu = findChild(target, "recentMenu")
            tryVerify(() => menu.visible)
            const remove = findChild(target, "recentRemove")
            verify(remove && remove.enabled)
            verify(findChild(target, "recentReveal").enabled)
            remove.triggered()
            menu.close()
            compare(paths(c), [beta])
            verify(testFiles.exists(alpha), "the file itself stays")
            tryVerify(() => findChild(home, "recentProject:" + alpha) === null)
            // A missing one can be removed too; one not listed isn't.
            verify(testFiles.move(beta, folder + "/gone/Beta.tve"))
            c.refreshRecentProjects()
            verify(c.removeRecentProject(beta))
            verify(!c.removeRecentProject(beta))
            compare(c.recentProjects.length, 0)
            verify(findChild(home, "recentEmpty").visible)
            // Kept that way for the next start.
            compare(controller().recentProjects.length, 0)
        }

        // Two windows (controllers) at once: neither puts back what the other took off.
        function test_two_controllers_share_the_list() {
            const a = controller()
            const alpha = saved(a, "Alpha")
            const b = controller()
            verify(b.removeRecentProject(alpha))
            const beta = saved(a, "Beta")
            compare(paths(a), [beta])
            verify(b.open("file://" + beta))
            b.refreshRecentProjects()
            compare(paths(b), [beta])
        }

        function test_a_card_opens_its_project() {
            const c = controller()
            const alpha = saved(c, "Alpha")
            saved(c, "Beta")
            c.newProject(1280, 720)
            const home = homeWith(c)
            const spy = createTemporaryObject(spyComponent, testCase, { target: home, signalName: "recentRequested" })
            const target = card(home, alpha)
            compare(findChild(target, "recentName").text, "Alpha")
            mouseClick(target)
            compare(spy.count, 1)
            compare(spy.signalArguments[0][0], alpha)
            // What the app does with it (app/Main.qml).
            verify(c.openRecentProject(spy.signalArguments[0][0]))
            compare(c.filePath, alpha)
            compare(c.name, "Alpha")
            verify(c.duration > 1)
            compare(c.recentProjects[0].path, alpha)
        }

        function test_projects_page_searches_and_sorts() {
            const c = controller()
            const zebra = saved(c, "Zebra trip")
            const apple = saved(c, "Apple launch")
            const mango = saved(c, "Mango")
            const home = homeWith(c)
            verify(!findChild(home, "projectsPage").visible)
            mouseClick(findChild(home, "homeRail:projects"))
            verify(findChild(home, "projectsPage").visible)
            verify(!findChild(home, "homePage").visible)
            compare(findChild(home, "projectsCount").text, "3 projects opened or saved lately")
            // Newest first, then by name.
            compare(home.listedProjects.map((p) => p.name), ["Mango", "Apple launch", "Zebra trip"])
            home.projectSort = "name"
            compare(home.listedProjects.map((p) => p.name), ["Apple launch", "Mango", "Zebra trip"])
            // Search by name (any case) or folder.
            const search = findChild(home, "projectSearch")
            mouseClick(search)
            keyClick(Qt.Key_Z)
            keyClick(Qt.Key_E)
            compare(home.projectSearch, "ze")
            compare(home.listedProjects.map((p) => p.name), ["Zebra trip"])
            card(home, zebra)
            home.projectSearch = "nothing like it"
            compare(home.listedProjects.length, 0)
            compare(findChild(home, "projectsEmpty").text, "No projects match “nothing like it”")
            home.projectSearch = folder.split("/").pop()
            compare(home.listedProjects.length, 3)
            // A card here opens too; Home is back from the rail.
            const spy = createTemporaryObject(spyComponent, testCase, { target: home, signalName: "recentRequested" })
            mouseClick(card(home, apple))
            compare(spy.signalArguments[0][0], apple)
            mouseClick(findChild(home, "homeRail:home"))
            verify(findChild(home, "homePage").visible)
            // Templates and Recordings aren't pages yet.
            verify(!findChild(home, "homeRail:templates").enabled)
            verify(!findChild(home, "homeRail:record").enabled)
        }

        function test_home_shows_the_latest_few() {
            const c = controller()
            const all = []
            for (let i = 0; i < 10; ++i)
                all.push(saved(c, "Project " + i))
            const home = homeWith(c)
            card(home, all[9])
            card(home, all[2])
            compare(findChild(home, "recentProject:" + all[1]), null) // on the Projects page
            const more = findChild(home, "allProjects")
            verify(more.visible)
            mouseClick(more)
            compare(home.page, "projects")
            card(home, all[0])
        }
    }
}
