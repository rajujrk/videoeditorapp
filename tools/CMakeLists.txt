add_executable(tve_decode_bench decode_bench/main.cpp)
target_link_libraries(tve_decode_bench PRIVATE tve::engine)
tve_set_warnings(tve_decode_bench)
