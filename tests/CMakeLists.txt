add_executable(tve_core_tests
    core/test_time.cpp
    core/test_commands.cpp
    core/test_serializer.cpp
    core/test_effect_manifest.cpp
    core/test_timeline_query.cpp
    core/test_audio_model.cpp
    core/test_visual_model.cpp
    core/test_time_model.cpp
    core/test_transitions.cpp
    core/test_titles.cpp
)
target_link_libraries(tve_core_tests PRIVATE tve::core Catch2::Catch2WithMain)
catch_discover_tests(tve_core_tests)

add_executable(tve_engine_tests
    engine/test_ffmpeg_license.cpp
    engine/test_decoder.cpp
    engine/test_frame_queue.cpp
    engine/test_timeline_playback.cpp
    engine/test_audio.cpp
    engine/test_mp4_writer.cpp
    engine/test_proxy.cpp
)
target_link_libraries(tve_engine_tests PRIVATE tve::engine tve::ffmpeg Catch2::Catch2WithMain)
target_compile_definitions(tve_engine_tests PRIVATE TVE_SAMPLES_DIR="${PROJECT_SOURCE_DIR}/samples")
catch_discover_tests(tve_engine_tests)

if(TARGET tve_gpu)
    qt_add_executable(tve_gpu_tests gpu/test_effects.cpp gpu/test_compositor.cpp gpu/test_transitions.cpp)
    # Deliberately broken effect and transition packages for the loader's error checks.
    file(GLOB TVE_TEST_EFFECT_SHADERS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
         gpu/fixtures/video/*/*.frag gpu/fixtures/transitions/*/*.frag)
    file(GLOB TVE_TEST_EFFECT_MANIFESTS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
         gpu/fixtures/video/*/manifest.json gpu/fixtures/transitions/*/manifest.json)
    qt_add_shaders(tve_gpu_tests "tve_test_effect_shaders"
        PREFIX "/test_effects"
        BASE gpu/fixtures
        FILES ${TVE_TEST_EFFECT_SHADERS}
    )
    qt_add_resources(tve_gpu_tests "tve_test_effect_manifests"
        PREFIX "/test_effects"
        BASE gpu/fixtures
        FILES ${TVE_TEST_EFFECT_MANIFESTS}
    )
    set_target_properties(tve_gpu_tests PROPERTIES MACOSX_BUNDLE OFF)
    target_link_libraries(tve_gpu_tests PRIVATE tve::gpu Catch2::Catch2WithMain)
    catch_discover_tests(tve_gpu_tests)
endif()

if(TARGET tve_shell)
    find_package(Qt6 REQUIRED COMPONENTS QuickTest)
    qt_add_executable(tve_qml_tests qml/main.cpp)
    set_target_properties(tve_qml_tests PROPERTIES MACOSX_BUNDLE OFF)
    target_compile_definitions(tve_qml_tests PRIVATE
        TVE_QML_TESTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml"
        TVE_SAMPLES_DIR="${PROJECT_SOURCE_DIR}/samples")
    target_link_libraries(tve_qml_tests PRIVATE Qt6::QuickTest Qt6::QuickControls2 tve_shellplugin)
    target_include_directories(tve_qml_tests PRIVATE $<TARGET_PROPERTY:tve_player,INTERFACE_INCLUDE_DIRECTORIES>)
    add_test(NAME tve_qml_tests COMMAND tve_qml_tests)
endif()
