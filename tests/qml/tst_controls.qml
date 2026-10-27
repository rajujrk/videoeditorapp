import QtQuick
import QtTest
import TechieVideoEditor.Theme

// Design-system controls that are not covered by the screen tests.
Item {
    id: root
    width: 300
    height: 120

    ToggleSwitch { id: bare }
    ToggleSwitch { id: labelled; y: 40; text: "Reverse" }

    TestCase {
        name: "Controls"
        when: windowShown

        function test_switch_without_a_label_is_clickable() {
            verify(bare.width >= 32 && bare.height >= 18)
            mouseClick(bare)
            verify(bare.checked)
        }

        function test_labelled_switch_makes_room_for_its_label() {
            verify(labelled.width > bare.width + 20)
            mouseClick(labelled, labelled.width - 4, labelled.height / 2) // on the label
            verify(labelled.checked)
        }
    }
}
