qt_add_library(tve_shell STATIC)

qt_add_qml_module(tve_shell
    URI TechieVideoEditor.Shell
    VERSION 1.0
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml/TechieVideoEditor/Shell
    QML_FILES
        HomeScreen.qml
        EditorScreen.qml
        TopBar.qml
        LibraryPanel.qml
        PlayerPanel.qml
        InspectorPanel.qml
        TimelinePanel.qml
        ClipEffectCard.qml
        ParamControl.qml
        ExportDialog.qml
        VoiceOverPanel.qml
        KeyframeToggle.qml
        CurveEditor.qml
    IMPORTS
        TechieVideoEditor.Theme
        TechieVideoEditor.Player
)

target_link_libraries(tve_shell PRIVATE Qt6::Quick tve_themeplugin tve_playerplugin)
