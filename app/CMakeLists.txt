qt_add_executable(TechieVideoEditor MACOSX_BUNDLE main.cpp)

qt_add_qml_module(TechieVideoEditor
    URI TechieVideoEditor.App
    VERSION 1.0
    QML_FILES
        Main.qml
    IMPORTS
        TechieVideoEditor.Theme
        TechieVideoEditor.Shell
)

set_target_properties(TechieVideoEditor PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.techievideoeditor.app"
    MACOSX_BUNDLE_BUNDLE_NAME "TechieVideoEditor"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
)

target_compile_definitions(TechieVideoEditor PRIVATE TVE_VERSION="${PROJECT_VERSION}")
# Static QML modules are linked through their plugin targets. The shell plugin brings in
# the theme and player plugins (and their backing libraries); linking them again here
# triggers duplicate-library warnings, so only the player's include path is borrowed.
target_link_libraries(TechieVideoEditor PRIVATE
    Qt6::Quick
    Qt6::QuickControls2
    tve_shellplugin
)
target_include_directories(TechieVideoEditor PRIVATE $<TARGET_PROPERTY:tve_player,INTERFACE_INCLUDE_DIRECTORIES>)
tve_set_warnings(TechieVideoEditor)
