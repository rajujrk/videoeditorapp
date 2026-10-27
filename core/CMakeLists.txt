add_library(tve_core STATIC
    model/Time.h
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
    content/EffectManifest.h
    content/EffectManifest.cpp
)
add_library(tve::core ALIAS tve_core)

target_include_directories(tve_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(tve_core PUBLIC nlohmann_json::nlohmann_json)
tve_set_warnings(tve_core)
