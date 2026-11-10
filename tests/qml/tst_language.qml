import QtQuick
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell

// The app in Tamil and back: chosen on the Home screen (kept in the scratch language file),
// everything on screen retranslated at once — QML text, the C++ messages and names (undo
// steps, effects, shortcuts, caption languages) — and numbers, dates and timecode written
// the language's way.
Item {
    id: root
    width: 1400
    height: 780

    Component { id: homeComponent; HomeScreen { anchors.fill: parent } }
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
        name: "Language"
        when: windowShown

        function cleanup() { Language.language = "en" }

        function settings() { return JSON.parse(testFiles.read(languageFile)) }

        function test_chosen_on_the_home_screen() {
            compare(Language.current, "en")
            const home = createTemporaryObject(homeComponent, root)
            waitForRendering(home)
            const picker = findChild(home, "languagePicker")
            compare(picker.currentValue, "en")
            compare(Language.languages.map((l) => l.id), ["auto", "en", "ta"])
            compare(Language.languages[2].label, "தமிழ்")

            picker.currentIndex = 2
            picker.activated(2)
            compare(Language.language, "ta")
            compare(Language.current, "ta")
            compare(settings().language, "ta")
            // Retranslated in place.
            compare(findChild(home, "newProjectHeading").text, "புதிய திட்டம்")
            compare(Language.languages[0].label, "இந்த Mac-இன் மொழி")

            Language.language = "auto"
            verify(["en", "ta"].indexOf(Language.current) >= 0)
            compare(settings().language, "auto")
        }

        function test_the_editor_in_tamil() {
            Language.language = "ta"
            const f = createTemporaryObject(editorComponent, root)
            waitForRendering(f)
            const p = f.project
            // QML text.
            compare(findChild(f.editor, "exportButton").text, "ஏற்றுமதி")
            // Object names stay English; what they show doesn't.
            compare(findChild(f.editor, "inspectorTab:Effects").contentItem.text, "விளைவுகள்")
            // A message from the controller.
            verify(!p.splitAtPlayhead())
            compare(p.lastError, "பிரிக்க, இயக்கக்குறியை ஒரு கிளிப்புக்குள் வைக்கவும்")
            // C++ names: effects, shortcuts, caption languages, auto-edit styles.
            verify(f.editor.player.availableEffects.some((e) => e.title === "கருப்பு வெள்ளை"))
            verify(KeyMap.actions.some((a) => a.title === "செயல்தவிர்"))
            verify(p.captionLanguages.some((l) => l.code === "ta" && l.name === "தமிழ்"))
            verify(p.autoEditStyles.some((s) => s.name === "பழங்காலம்"))
            verify(p.layouts.some((l) => l.title === "படத்துக்குள் படம்"))
            // An undo step, named in the core, shown in Tamil.
            verify(p.addTitle({ text: "வணக்கம்" }))
            compare(p.undoText, "தலைப்பைச் சேர்")


            // Back to English at once, with no new window.
            Language.language = "en"
            compare(findChild(f.editor, "exportButton").text, "Export")
            tryVerify(() => f.editor.player.availableEffects.some((e) => e.title === "Black & White"))
            compare(p.undoText, "Add Title")
            verify(KeyMap.actions.some((a) => a.title === "Undo"))
        }

        function test_numbers_dates_and_timecode() {
            Language.language = "ta"
            verify(Qt.locale().name.indexOf("ta") === 0, Qt.locale().name)
            compare(Fmt.num(1234.5, 1), "1234.5")
            compare(Fmt.percent(0.42), "42 %")
            compare(Fmt.seconds(2.5), "2.5 வி")
            compare(Qt.locale().monthName(10), "நவம்பர்")
            verify(Fmt.date(new Date(2026, 10, 10)).indexOf("26") >= 0)
            const f = createTemporaryObject(editorComponent, root)
            compare(f.project.timecode(0), "00:00:00:00")
            Language.language = "en"
            compare(Qt.locale().monthName(10), "November")
            compare(Fmt.seconds(2.5), "2.5 s")
        }
    }
}
