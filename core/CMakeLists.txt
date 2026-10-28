add_library(tve_core STATIC
    model/Time.h
    model/Keyframes.h
    model/Keyframes.cpp
    model/SpeedRamp.h
    model/SpeedRamp.cpp
    model/ColorGrade.h
    model/ColorGrade.cpp
    model/Masks.h
    model/Masks.cpp
    model/TextAnimation.h
    model/TextAnimation.cpp
    model/Project.h
    model/Project.cpp
    model/TimelineQuery.h
    model/TimelineQuery.cpp
    commands/Command.h
    commands/CommandStack.h
    commands/CommandStack.cpp
    commands/EditCommands.h
    commands/EditCommands.cpp
    io/ProjectSerializer.h
    io/ProjectSerializer.cpp
    io/LutFile.h
    io/LutFile.cpp
    content/EffectManifest.h
    content/EffectManifest.cpp
)
add_library(tve::core ALIAS tve_core)

target_include_directories(tve_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(tve_core PUBLIC nlohmann_json::nlohmann_json)
tve_set_warnings(tve_core)
