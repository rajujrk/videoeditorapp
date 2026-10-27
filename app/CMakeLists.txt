qt_add_executable(TechieVideoEditor MACOSX_BUNDLE main.cpp)

qt_add_qml_module(TechieVideoEditor
    URI TechieVideoEditor.App
    VERSION 1.0
    QML_FILES Main.qml
)

set_target_properties(TechieVideoEditor PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.techievideoeditor.app"
    MACOSX_BUNDLE_BUNDLE_NAME "TechieVideoEditor"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
)

target_compile_definitions(TechieVideoEditor PRIVATE TVE_VERSION="${PROJECT_VERSION}")
# The plugin target links tve_player itself; linking both triggers a duplicate-library
# warning, so only borrow the player's include path here.
target_link_libraries(TechieVideoEditor PRIVATE Qt6::Quick tve_playerplugin)
target_include_directories(TechieVideoEditor PRIVATE $<TARGET_PROPERTY:tve_player,INTERFACE_INCLUDE_DIRECTORIES>)
tve_set_warnings(TechieVideoEditor)
