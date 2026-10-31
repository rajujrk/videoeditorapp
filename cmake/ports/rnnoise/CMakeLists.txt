cmake_minimum_required(VERSION 3.20)
project(rnnoise C)

# The library sources of rnnoise 0.2 (Makefile.am), without the x86 run-time CPU dispatch:
# on arm the NEON paths are chosen at compile time.
add_library(rnnoise
    src/denoise.c
    src/rnn.c
    src/pitch.c
    src/kiss_fft.c
    src/celt_lpc.c
    src/nnet.c
    src/nnet_default.c
    src/parse_lpcnet_weights.c
    src/rnnoise_data.c
    src/rnnoise_tables.c
)
target_include_directories(rnnoise
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>
    PRIVATE src)
set_target_properties(rnnoise PROPERTIES C_STANDARD 99 POSITION_INDEPENDENT_CODE ON)
if(NOT MSVC)
    target_link_libraries(rnnoise PRIVATE m)
endif()

install(TARGETS rnnoise EXPORT rnnoise-targets ARCHIVE DESTINATION lib LIBRARY DESTINATION lib RUNTIME DESTINATION bin)
install(FILES include/rnnoise.h DESTINATION include)
install(EXPORT rnnoise-targets NAMESPACE rnnoise:: DESTINATION lib/cmake/rnnoise)
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/rnnoise-config.cmake "include(\"\${CMAKE_CURRENT_LIST_DIR}/rnnoise-targets.cmake\")\n")
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/rnnoise-config.cmake DESTINATION lib/cmake/rnnoise)
