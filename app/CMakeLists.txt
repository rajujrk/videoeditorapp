# The icon (drawn by tools/make_icon.swift), in the bundle's Resources.
set(TVE_APP_ICON ${CMAKE_CURRENT_SOURCE_DIR}/AppIcon.icns)
set_source_files_properties(${TVE_APP_ICON} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
qt_add_executable(TechieVideoEditor MACOSX_BUNDLE main.cpp ${TVE_APP_ICON})

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
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
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

# The VST3 scanner runs beside the app (Contents/MacOS), each bundle checked in its own
# process.
add_dependencies(TechieVideoEditor tve_pluginscan)
add_custom_command(TARGET TechieVideoEditor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:tve_pluginscan> $<TARGET_FILE_DIR:TechieVideoEditor>/tve-pluginscan
    COMMENT "Copying tve-pluginscan into the app")
