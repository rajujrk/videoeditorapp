cmake_minimum_required(VERSION 3.25)

project(TechieVideoEditor
    VERSION 0.0.1
    DESCRIPTION "TechieVideoEditor desktop video editor"
    LANGUAGES C CXX)

if(APPLE)
    enable_language(OBJCXX)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "Minimum macOS version")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_OBJCXX_STANDARD 20)

option(TVE_BUILD_APP "Build the Qt application (needs Qt 6.7+)" ON)
option(TVE_BUILD_TESTS "Build unit tests" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(TveWarnings)

find_package(nlohmann_json CONFIG REQUIRED)
# vcpkg's FFmpeg wrapper sets FFMPEG_INCLUDE_DIRS / FFMPEG_LIBRARIES (static libs plus their
# system dependencies). Wrap them in one target the rest of the build links against.
find_package(FFMPEG REQUIRED)
add_library(tve_ffmpeg INTERFACE)
add_library(tve::ffmpeg ALIAS tve_ffmpeg)
target_include_directories(tve_ffmpeg INTERFACE ${FFMPEG_INCLUDE_DIRS})
target_link_directories(tve_ffmpeg INTERFACE ${FFMPEG_LIBRARY_DIRS})
target_link_libraries(tve_ffmpeg INTERFACE ${FFMPEG_LIBRARIES})

if(TVE_BUILD_TESTS)
    enable_testing()
    find_package(Catch2 3 CONFIG REQUIRED)
    include(Catch)
endif()

add_subdirectory(core)
add_subdirectory(engine)
add_subdirectory(tools)

if(TVE_BUILD_APP)
    find_package(Qt6 6.7 REQUIRED COMPONENTS Core Gui GuiPrivate Quick QuickControls2 ShaderTools Network)
    qt_standard_project_setup(REQUIRES 6.7)
    add_subdirectory(effects)
    add_subdirectory(engine/gpu)
    add_subdirectory(ui/theme)
    add_subdirectory(ui/player)
    add_subdirectory(ui/shell)
    add_subdirectory(app)
endif()

if(TVE_BUILD_TESTS)
    add_subdirectory(tests)
endif()
