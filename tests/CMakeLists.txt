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
    core/test_voiceover.cpp
    core/test_keyframes.cpp
    core/test_speed_ramp.cpp
    core/test_color.cpp
    core/test_looks.cpp
    core/test_stills.cpp
    core/test_masks.cpp
    core/test_structure.cpp
    core/test_relink.cpp
    core/test_ranges.cpp
    core/test_captions.cpp
    core/test_people.cpp
    core/test_motion.cpp
)
target_link_libraries(tve_core_tests PRIVATE tve::core Catch2::Catch2WithMain)
catch_discover_tests(tve_core_tests)

add_executable(tve_engine_tests
    engine/test_ffmpeg_license.cpp
    engine/test_decoder.cpp
    engine/test_frame_queue.cpp
    engine/test_timeline_playback.cpp
    engine/test_audio.cpp
    engine/test_media_writer.cpp
    engine/test_proxy.cpp
    engine/test_recorder.cpp
    engine/test_scopes.cpp
    engine/test_audio_dsp.cpp
    engine/test_stickers.cpp
    engine/test_media_analysis.cpp
    engine/test_transcriber.cpp
    engine/test_speech_enhancer.cpp
    engine/test_people_vision.cpp
    engine/test_motion_analysis.cpp
    engine/test_multicam.cpp
    engine/test_video_processing.cpp
    engine/test_capture.cpp
)
target_link_libraries(tve_engine_tests PRIVATE tve::engine tve::ffmpeg Catch2::Catch2WithMain)
target_compile_definitions(tve_engine_tests PRIVATE TVE_SAMPLES_DIR="${PROJECT_SOURCE_DIR}/samples"
                                                   TVE_EFFECTS_DIR="${PROJECT_SOURCE_DIR}/effects")
# Tests tagged [apple] work Vision and VideoToolbox hard (GPU, Neural Engine): they share
# the GPU lock with the UI and golden-frame tests rather than slowing them down.
catch_discover_tests(tve_engine_tests TEST_SPEC "~[apple]")
catch_discover_tests(tve_engine_tests TEST_SPEC "[apple]" PROPERTIES RESOURCE_LOCK gpu)

if(TARGET tve_gpu)
    qt_add_executable(tve_gpu_tests gpu/test_effects.cpp gpu/test_compositor.cpp gpu/test_transitions.cpp gpu/test_color.cpp gpu/test_looks.cpp gpu/test_masks.cpp gpu/test_effect_library.cpp)
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

# Golden frames: fixed projects rendered by the real renderer and compared with reference
# pictures (tests/golden/metal). TVE_GOLDEN_UPDATE=1 rewrites the references.
if(TARGET tve_player)
    qt_add_executable(tve_golden_tests golden/test_golden.cpp)
    set_target_properties(tve_golden_tests PROPERTIES MACOSX_BUNDLE OFF)
    target_compile_definitions(tve_golden_tests PRIVATE TVE_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden")
    target_include_directories(tve_golden_tests PRIVATE $<TARGET_PROPERTY:tve_player,INTERFACE_INCLUDE_DIRECTORIES>)
    target_link_libraries(tve_golden_tests PRIVATE tve_player tve::engine tve::gpu Qt6::Gui Catch2::Catch2)
    add_test(NAME tve_golden_tests COMMAND tve_golden_tests)
    # Heavy GPU use: not at the same time as the UI tests, whose timings it would upset.
    set_tests_properties(tve_golden_tests PROPERTIES RESOURCE_LOCK gpu)
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
    set_tests_properties(tve_qml_tests PROPERTIES RESOURCE_LOCK gpu)
endif()
