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
        SourceMonitor.qml
        GuidesOverlay.qml
        CompareOverlay.qml
        InspectorPanel.qml
        TimelinePanel.qml
        TimelineMenus.qml
        ConfirmDialog.qml
        ClipEffectCard.qml
        ParamControl.qml
        ExportDialog.qml
        RenderQueuePanel.qml
        RelinkDialog.qml
        ShortcutsDialog.qml
        AnalyzePage.qml
        MediaSearch.qml
        AudioPage.qml
        CaptionsPage.qml
        CloudSettingsDialog.qml
        WritePanel.qml
        VoicePane.qml
        GeneratePage.qml
        TemplatesPage.qml
        ShapePicker.qml
        SaveTemplateDialog.qml
        AutoEditPanel.qml
        ImportPanel.qml
        TranscriptPage.qml
        VoiceOverPanel.qml
        KeyframeToggle.qml
        CurveEditor.qml
        SpeedRampEditor.qml
        ColorWheelControl.qml
        ToneCurveEditor.qml
        MaskOverlay.qml
        TransformOverlay.qml
        DrawingOverlay.qml
        StorePage.qml
        TrackOverlay.qml
        RecordPanel.qml
        MixerPanel.qml
        HdrViewer.qml
        TeleprompterWindow.qml
        LiveDrawing.qml
        ScreenAreaPicker.qml
        RecordingFrame.qml
        RecordCountdown.qml
        ProjectSettingsDialog.qml
        ImageSequenceDialog.qml
        KeyboardMap.qml
        FavouriteBadge.qml
        PreviewWindow.qml
        SphereOverlay.qml
        InterchangeDialog.qml
        CopilotPage.qml
        VoiceCloneDialog.qml
        TitleTemplates.js
    IMPORTS
        TechieVideoEditor.Theme
        TechieVideoEditor.Player
)

target_link_libraries(tve_shell PRIVATE Qt6::Quick tve_themeplugin tve_playerplugin)
