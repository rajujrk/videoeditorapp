# Built-in effect packages compiled into the app as Qt resources:
#   :/effects/common/fullscreen.vert.qsb
#   :/effects/video/<folder>/manifest.json
#   :/effects/video/<folder>/<pass>.frag.qsb
#   :/effects/transitions/<folder>/manifest.json
#   :/effects/transitions/<folder>/<pass>.frag.qsb
#   :/effects/looks/looks.json and :/effects/looks/luts/<look>.cube (tools/make_looks.py)
# Shaders that fail to compile fail the build.

file(GLOB TVE_EFFECT_MANIFESTS CONFIGURE_DEPENDS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
     video/*/manifest.json transitions/*/manifest.json)
file(GLOB TVE_LOOK_FILES CONFIGURE_DEPENDS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} looks/looks.json looks/luts/*.cube)
file(GLOB TVE_EFFECT_SHADERS CONFIGURE_DEPENDS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} video/*/*.frag transitions/*/*.frag)

qt_add_library(tve_effects STATIC BuiltinEffects.cpp)

qt_add_shaders(tve_effects "tve_effect_shaders"
    PREFIX "/effects"
    FILES
        common/fullscreen.vert
        ${TVE_EFFECT_SHADERS}
)

qt_add_resources(tve_effects "tve_effect_manifests"
    PREFIX "/effects"
    FILES ${TVE_EFFECT_MANIFESTS} ${TVE_LOOK_FILES}
)

target_link_libraries(tve_effects PRIVATE Qt6::Core)
