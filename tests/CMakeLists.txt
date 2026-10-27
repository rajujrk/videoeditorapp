add_executable(tve_core_tests
    core/test_time.cpp
    core/test_commands.cpp
    core/test_serializer.cpp
)
target_link_libraries(tve_core_tests PRIVATE tve::core Catch2::Catch2WithMain)
catch_discover_tests(tve_core_tests)

add_executable(tve_engine_tests
    engine/test_ffmpeg_license.cpp
    engine/test_decoder.cpp
    engine/test_frame_queue.cpp
)
target_link_libraries(tve_engine_tests PRIVATE tve::engine tve::ffmpeg Catch2::Catch2WithMain)
target_compile_definitions(tve_engine_tests PRIVATE TVE_SAMPLES_DIR="${PROJECT_SOURCE_DIR}/samples")
catch_discover_tests(tve_engine_tests)
