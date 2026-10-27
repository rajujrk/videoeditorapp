qt_add_library(tve_theme STATIC)

set_source_files_properties(Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

file(GLOB TVE_THEME_ICONS CONFIGURE_DEPENDS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} icons/*.svg)

qt_add_qml_module(tve_theme
    URI TechieVideoEditor.Theme
    VERSION 1.0
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml/TechieVideoEditor/Theme
    QML_FILES
        Theme.qml
        Icon.qml
        IconButton.qml
        TextButton.qml
        SearchField.qml
        PanelHeader.qml
        Divider.qml
        Section.qml
        PlaceholderBox.qml
        ToggleSwitch.qml
    RESOURCES
        ${TVE_THEME_ICONS}
)

target_link_libraries(tve_theme PRIVATE Qt6::Quick)
