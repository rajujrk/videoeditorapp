# GPU effect processing on Qt RHI. Separate from tve_engine because it depends on Qt.
add_library(tve_gpu STATIC
    EffectLibrary.h
    EffectLibrary.cpp
    EffectChain.h
    EffectChain.cpp
)
add_library(tve::gpu ALIAS tve_gpu)

target_include_directories(tve_gpu PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(tve_gpu
    PUBLIC
        tve::core
        Qt6::Gui
        Qt6::GuiPrivate
        tve_effects
)
tve_set_warnings(tve_gpu)
