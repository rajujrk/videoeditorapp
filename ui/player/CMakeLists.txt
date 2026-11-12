qt_add_library(tve_player STATIC)

qt_add_qml_module(tve_player
    URI TechieVideoEditor.Player
    VERSION 1.0
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml/TechieVideoEditor/Player
    SOURCES
        VideoView.h
        VideoView.mm
        FrameCompositor.h
        FrameCompositor.mm
        Exporter.h
        Exporter.mm
        StillRenderer.h
        StillRenderer.mm
        ProxyManager.h
        ProxyManager.cpp
        ProjectController.h
        ProjectController.cpp
        Copilot.h
        Copilot.cpp
        Language.h
        Language.cpp
        CatalogueStrings.cpp
        RenderCache.h
        RenderCache.cpp
        VoiceCloning.h
        VoiceCloning.cpp
        KeyMap.h
        KeyMap.cpp
        SpeechModels.h
        SpeechModels.cpp
        SoundLibrary.h
        SoundLibrary.cpp
        MediaImports.h
        MediaImports.cpp
        TemplateLibrary.h
        TemplateLibrary.cpp
        AutoEdit.h
        AutoEdit.cpp
        cloud/CloudKeys.h
        cloud/CloudKeys.mm
        cloud/CloudText.h
        cloud/CloudText.cpp
        cloud/CloudVoices.h
        cloud/CloudVoices.mm
        cloud/CloudFeatures.h
        cloud/CloudFeatures.cpp
        cloud/CloudMedia.h
        cloud/CloudMedia.cpp
        cloud/StickerCutout.h
        cloud/StickerCutout.mm
        cloud/CloudServices.h
        cloud/CloudServices.cpp
        cloud/MockCloudServer.h
        cloud/MockCloudServer.cpp
        WaveformItem.h
        WaveformItem.cpp
        TrackListModel.h
        TrackListModel.cpp
        ThumbnailProvider.h
        ThumbnailProvider.cpp
        EffectInfo.h
        EffectInfo.cpp
        TransitionPreviewProvider.h
        TransitionPreviewProvider.cpp
        EffectPreviewProvider.h
        EffectPreviewProvider.cpp
        Stickers.h
        Stickers.cpp
        PreviewImages.h
        PreviewImages.cpp
        TitleRenderer.h
        TitleRenderer.cpp
        DrawingRenderer.h
        DrawingRenderer.cpp
        OverlayTransitions.h
        OverlayTransitions.cpp
        PackStore.h
        AudioPluginHost.h
        AudioPluginHost.mm
        PackStore.cpp
        StillImage.h
        StillImage.cpp
        LookLibrary.h
        LookLibrary.cpp
        Teleprompter.h
        Teleprompter.cpp
        NativeWindow.h
        NativeWindow.mm
        ScopeView.h
        ScopeView.cpp
        MicrophoneAccess.h
        MicrophoneAccess.mm
)

# The built-in template recipes (effects/templates/<name>/template.json) as :/templates/<name>/template.json.
file(GLOB TVE_TEMPLATE_RECIPES CONFIGURE_DEPENDS RELATIVE ${CMAKE_SOURCE_DIR}/effects/templates
     ${CMAKE_SOURCE_DIR}/effects/templates/*/template.json)
foreach(recipe IN LISTS TVE_TEMPLATE_RECIPES)
    set_source_files_properties(${CMAKE_SOURCE_DIR}/effects/templates/${recipe} PROPERTIES QT_RESOURCE_ALIAS ${recipe})
endforeach()
list(TRANSFORM TVE_TEMPLATE_RECIPES PREPEND ${CMAKE_SOURCE_DIR}/effects/templates/)
qt_add_resources(tve_player "tve_templates" PREFIX "/templates" FILES ${TVE_TEMPLATE_RECIPES})

qt_add_shaders(tve_player "tve_player_shaders"
    PREFIX "/"
    FILES
        shaders/video.vert
        shaders/video.frag
        shaders/present.frag
        shaders/export_yuv.frag
)

qt_add_resources(tve_player "tve_player_data"
    PREFIX "/"
    FILES
        export_presets.json
)

# The translations (i18n/*.ts, filled by the update_translations target), compiled and
# built in at :/i18n/.
find_package(Qt6 REQUIRED COMPONENTS LinguistTools)
qt_add_lrelease(
    TS_FILES ${CMAKE_SOURCE_DIR}/i18n/tve_ta.ts
    LRELEASE_TARGET tve_translations
    QM_FILES_OUTPUT_VARIABLE TVE_QM_FILES)
qt_add_resources(tve_player "tve_translations" PREFIX "/i18n" BASE ${CMAKE_CURRENT_BINARY_DIR} FILES ${TVE_QM_FILES})

target_include_directories(tve_player PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# (QML type registration includes headers by their bare name.)
target_include_directories(tve_player PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/cloud)
target_link_libraries(tve_player
    PUBLIC
        Qt6::Quick
        tve::gpu # VideoView.h exposes tve::EffectInstance
    PRIVATE
        Qt6::GuiPrivate
        Qt6::Network
        tve::engine
        "-framework Metal"
        "-framework CoreVideo"
        "-framework AVFoundation"
        "-framework AVFAudio"
        "-framework Security"
)
