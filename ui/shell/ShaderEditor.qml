import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import TechieVideoEditor.Theme
import TechieVideoEditor.Player

// The shader editor: your own GPU effect, written as `vec4 effect(vec2 uv)` in GLSL with
// settings of your own (sliders and colours, used in the code by their names). It compiles as
// you type, the preview showing it on the app's sample scene (moving, so tve_time shows) or
// what went wrong, by line. Saved, it joins the Effects tab under Mine, for clips like any other.
Popup {
    id: editor
    required property var project
    // The effect being edited ("" for a new one), and what the editor has of it.
    property string effectId: ""
    property var params: []
    property string previewId: ""
    property string error: ""
    property bool compiling: false

    readonly property string template: "// The colour at uv (0..1 across and down the picture). tve_source is the clip's\n"
        + "// picture and tve_time the seconds into it; your settings are here by name.\n"
        + "vec4 effect(vec2 uv)\n{\n    vec4 c = texture(tve_source, uv);\n"
        + "    float wave = 0.5 + 0.5 * sin(uv.x * 20.0 + tve_time * 3.0);\n"
        + "    vec3 tinted = mix(c.rgb, c.rgb * tint * (0.6 + 0.4 * wave), amount);\n"
        + "    return vec4(tinted, c.a);\n}\n"

    function openFor(id) {
        effectId = id
        const spec = id !== "" ? project.shaderEffectSpec(id) : null
        if (spec && spec.code !== undefined) {
            titleField.text = spec.title || ""
            code.text = spec.code
            params = (spec.params || []).map((p) => Object.assign({}, p))
        } else {
            effectId = ""
            titleField.text = qsTr("My effect")
            code.text = template
            params = [{ id: "amount", title: qsTr("Amount"), type: "float", min: 0, max: 1, default: 0.6 },
                      { id: "tint", title: qsTr("Tint"), type: "color", default: [1.0, 0.75, 0.55] }]
        }
        error = ""
        previewId = ""
        open()
        compileSoon.restart()
    }
    function spec() {
        return { id: effectId, title: titleField.text.trim(), code: code.text, params: params }
    }
    function compileNow() {
        const result = project.compileShaderEffect(spec())
        error = result.ok ? "" : result.error
        if (result.ok)
            previewId = result.previewId
    }
    function setParam(index, key, value) {
        const list = params.slice()
        list[index] = Object.assign({}, list[index])
        list[index][key] = value
        params = list
        compileSoon.restart()
    }
    function hexOf(rgb) {
        const c = (Array.isArray(rgb) || (rgb && rgb.length === 3) ? Array.from(rgb) : [1, 1, 1]).map((v) => Math.round(Math.max(0, Math.min(1, v)) * 255).toString(16).padStart(2, "0"))
        return "#" + c.join("")
    }
    function rgbOf(hex) {
        const m = /^#?([0-9a-f]{2})([0-9a-f]{2})([0-9a-f]{2})$/i.exec(hex.trim())
        return m ? [parseInt(m[1], 16) / 255, parseInt(m[2], 16) / 255, parseInt(m[3], 16) / 255] : null
    }

    Timer {
        id: compileSoon
        interval: 600
        onTriggered: editor.compileNow()
    }
    // The preview moving through two seconds of the sample scene.
    property real previewSeconds: 0
    Timer {
        running: editor.opened && editor.previewId !== ""
        repeat: true
        interval: 125
        onTriggered: editor.previewSeconds = editor.previewSeconds >= 1.875 ? 0 : editor.previewSeconds + 0.125
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(1040, (parent ? parent.width : 1040) - Theme.space.xl * 2)
    height: Math.min(700, (parent ? parent.height : 700) - Theme.space.xl * 2)
    modal: true
    padding: Theme.space.l
    closePolicy: Popup.CloseOnEscape
    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.motion.fast } }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.motion.fast } }
    background: Rectangle { radius: Theme.radius.l; color: Theme.color.surfaceRaised; border.color: Theme.color.borderStrong }

    contentItem: ColumnLayout {
        spacing: Theme.space.m
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.space.m
            Icon { name: "effects"; color: Theme.color.accentText }
            Text { text: editor.effectId === "" ? qsTr("New effect") : qsTr("Edit your effect"); font: Theme.type.title; color: Theme.color.textPrimary }
            TextField {
                id: titleField
                objectName: "shaderTitle"
                Layout.fillWidth: true
                placeholderText: qsTr("Its name")
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.space.l

            // The code.
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 3
                spacing: Theme.space.xs
                Text {
                    Layout.fillWidth: true
                    text: qsTr("GLSL: tve_source, tve_time, tve_texelSize, and tveLuma, tveNoise, tveSample are there for you.")
                    font: Theme.type.caption
                    color: Theme.color.textSecondary
                    wrapMode: Text.WordWrap
                }
                ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    TextArea {
                        id: code
                        objectName: "shaderCode"
                        font.family: Theme.type.mono.family
                        font.pixelSize: 13
                        wrapMode: TextEdit.NoWrap
                        selectByMouse: true
                        tabStopDistance: 28
                        color: Theme.color.textPrimary
                        background: Rectangle { color: Theme.color.surfaceSunken; radius: Theme.radius.m; border.color: Theme.color.borderSubtle }
                        onTextChanged: compileSoon.restart()
                        // Keywords, types, numbers, comments and the functions you can use, in colour.
                        GlslHighlighter {
                            objectName: "shaderHighlighter"
                            document: code.textDocument
                            keyword: Theme.color.accentText
                            type: Theme.color.success
                            number: Theme.color.warning
                            comment: Theme.color.textSecondary
                            builtin: Theme.color.trackTitle
                        }
                    }
                }
            }

            // The preview, what went wrong, and the settings.
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 2
                spacing: Theme.space.s
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: width * 9 / 16
                    radius: Theme.radius.m
                    color: Theme.color.surfaceSunken
                    clip: true
                    Image {
                        objectName: "shaderPreview"
                        anchors.fill: parent
                        fillMode: Image.PreserveAspectCrop
                        cache: false
                        source: editor.previewId !== "" ? "image://tveeffect/" + editor.previewId + "@" + editor.previewSeconds.toFixed(3) : ""
                        opacity: editor.error === "" ? 1 : 0.35
                        Behavior on opacity { NumberAnimation { duration: Theme.motion.fast } }
                    }
                    Text {
                        anchors.centerIn: parent
                        visible: editor.previewId === "" && editor.error === ""
                        text: qsTr("Compiling…")
                        font: Theme.type.caption
                        color: Theme.color.textSecondary
                    }
                }
                Text {
                    objectName: "shaderError"
                    Layout.fillWidth: true
                    visible: editor.error !== ""
                    text: editor.error
                    font: Theme.type.mono
                    color: Theme.color.danger
                    wrapMode: Text.WrapAnywhere
                    maximumLineCount: 6
                    elide: Text.ElideRight
                }
                Text { text: qsTr("Settings"); font: Theme.type.label; color: Theme.color.textSecondary }
                ScrollView {
                    id: settingsScroll
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    contentWidth: availableWidth
                    ColumnLayout {
                        width: settingsScroll.availableWidth
                        spacing: Theme.space.xs
                        Repeater {
                            model: editor.params
                            delegate: ColumnLayout {
                                id: paramRow
                                required property var modelData
                                required property int index
                                readonly property bool colour: modelData.type === "color"
                                objectName: "shaderParam:" + index
                                Layout.fillWidth: true
                                spacing: Theme.space.xxs
                                // Its name in the code, its title, its kind.
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: Theme.space.xs
                                    TextField {
                                        objectName: "shaderParamId:" + paramRow.index
                                        Layout.preferredWidth: 96
                                        text: paramRow.modelData.id
                                        placeholderText: qsTr("name")
                                        font.family: Theme.type.mono.family
                                        onEditingFinished: editor.setParam(paramRow.index, "id", text.trim())
                                    }
                                    TextField {
                                        Layout.fillWidth: true
                                        text: paramRow.modelData.title || ""
                                        placeholderText: qsTr("Title")
                                        onEditingFinished: editor.setParam(paramRow.index, "title", text)
                                    }
                                    AppComboBox {
                                        Layout.preferredWidth: 100
                                        model: [qsTr("Slider"), qsTr("Colour")]
                                        currentIndex: paramRow.colour ? 1 : 0
                                        onActivated: (i) => editor.setParam(paramRow.index, "type", i === 1 ? "color" : "float")
                                    }
                                    IconButton {
                                        iconName: "close"
                                        buttonSize: 24
                                        tip: qsTr("Remove this setting")
                                        onClicked: {
                                            const list = editor.params.slice()
                                            list.splice(paramRow.index, 1)
                                            editor.params = list
                                            compileSoon.restart()
                                        }
                                    }
                                }
                                // A slider's range and where it starts; a colour's (#rrggbb).
                                RowLayout {
                                    Layout.fillWidth: true
                                    Layout.leftMargin: 96 + Theme.space.xs
                                    spacing: Theme.space.xs
                                    Text { visible: !paramRow.colour; text: qsTr("From"); font: Theme.type.caption; color: Theme.color.textSecondary }
                                    TextField {
                                        visible: !paramRow.colour
                                        Layout.preferredWidth: 56
                                        text: String(paramRow.modelData.min !== undefined ? paramRow.modelData.min : 0)
                                        onEditingFinished: editor.setParam(paramRow.index, "min", Number(text))
                                    }
                                    Text { visible: !paramRow.colour; text: qsTr("to"); font: Theme.type.caption; color: Theme.color.textSecondary }
                                    TextField {
                                        visible: !paramRow.colour
                                        Layout.preferredWidth: 56
                                        text: String(paramRow.modelData.max !== undefined ? paramRow.modelData.max : 1)
                                        onEditingFinished: editor.setParam(paramRow.index, "max", Number(text))
                                    }
                                    Text { text: qsTr("starts at"); font: Theme.type.caption; color: Theme.color.textSecondary }
                                    Rectangle { // a colour's swatch
                                        visible: paramRow.colour
                                        width: 22
                                        height: 22
                                        radius: Theme.radius.s
                                        color: editor.hexOf(paramRow.modelData.default)
                                        border.color: Theme.color.borderStrong
                                    }
                                    TextField {
                                        Layout.preferredWidth: paramRow.colour ? 96 : 56
                                        text: paramRow.colour ? editor.hexOf(paramRow.modelData.default)
                                                              : String(paramRow.modelData.default !== undefined ? paramRow.modelData.default : 0)
                                        onEditingFinished: {
                                            if (paramRow.colour) {
                                                const rgb = editor.rgbOf(text)
                                                if (rgb)
                                                    editor.setParam(paramRow.index, "default", rgb)
                                            } else {
                                                editor.setParam(paramRow.index, "default", Number(text))
                                            }
                                        }
                                    }
                                    Item { Layout.fillWidth: true }
                                }
                            }
                        }
                        TextButton {
                            objectName: "shaderAddParam"
                            text: qsTr("Add a setting")
                            iconName: "plus"
                            variant: "ghost"
                            onClicked: {
                                let n = editor.params.length + 1
                                while (editor.params.some((p) => p.id === "value" + n))
                                    ++n
                                editor.params = editor.params.concat([{ id: "value" + n, title: qsTr("Value %1").arg(n), type: "float", min: 0, max: 1, default: 0.5 }])
                                compileSoon.restart()
                            }
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Text {
                Layout.fillWidth: true
                text: editor.error === "" && editor.previewId !== "" ? qsTr("Compiles: the preview is your effect on the sample scene.") : ""
                font: Theme.type.caption
                color: Theme.color.success
                elide: Text.ElideRight
            }
            TextButton { objectName: "shaderCheck"; text: qsTr("Check"); variant: "secondary"; onClicked: editor.compileNow() }
            TextButton { text: qsTr("Cancel"); variant: "ghost"; onClicked: editor.close() }
            TextButton {
                objectName: "shaderSave"
                text: qsTr("Save")
                variant: "primary"
                enabled: titleField.text.trim() !== ""
                onClicked: {
                    const id = editor.project.saveShaderEffect(editor.spec())
                    if (id === "") {
                        editor.error = editor.project.lastError
                        return
                    }
                    editor.effectId = id
                    editor.close()
                }
            }
        }
    }
}
