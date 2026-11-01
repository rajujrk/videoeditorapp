add_executable(tve_decode_bench decode_bench/main.cpp)
target_link_libraries(tve_decode_bench PRIVATE tve::engine)
tve_set_warnings(tve_decode_bench)

add_executable(tve_packtool packtool/main.cpp)
set_target_properties(tve_packtool PROPERTIES OUTPUT_NAME tve-packtool)
target_link_libraries(tve_packtool PRIVATE tve::engine nlohmann_json::nlohmann_json)
tve_set_warnings(tve_packtool)
