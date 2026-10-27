qt_add_library(tve_player STATIC)

qt_add_qml_module(tve_player
    URI TechieVideoEditor.Player
    VERSION 1.0
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml/TechieVideoEditor/Player
    SOURCES
        VideoView.h
        VideoView.mm
        ProjectController.h
        ProjectController.cpp
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
)

qt_add_shaders(tve_player "tve_player_shaders"
    PREFIX "/"
    FILES
        shaders/video.vert
        shaders/video.frag
        shaders/present.frag
)

target_include_directories(tve_player PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(tve_player
    PUBLIC
        Qt6::Quick
        tve::gpu # VideoView.h exposes tve::EffectInstance
    PRIVATE
        Qt6::GuiPrivate
        tve::engine
        "-framework Metal"
        "-framework CoreVideo"
)
