# RNNoise 0.2 with the model it was released with (its model_version, 0b50c45; vcpkg's own
# port fetches a later model meant for newer code, which runs but barely reduces noise),
# built with the CMakeLists.txt next to this file: the upstream port needs autotools and
# leaves out arm (its NEON path includes an os_support.h the release lacks; os_support.h
# here fills in). Only the library (no demo, no training tools).
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO xiph/rnnoise
    REF "v${VERSION}"
    SHA512 0f7de78494e0f2421c09871e9328437b64d021fd046c2198b836e84028995b43a56d113fb5ebc0bd76c1cb308a9cc53f67d6de5c1f67281248af492eab534bbc
    HEAD_REF main
)

vcpkg_download_distfile(
    MODEL_PATH
    URLS https://media.xiph.org/rnnoise/models/rnnoise_data-0b50c45.tar.gz
    FILENAME rnnoise_data-0b50c45.tar.gz
    SHA512 c15fef7c88d86264a29a3dab14d94bde769da68f255d131d135f6a40d94037b1ffe521f9e0a26339114750dbdd7cf774c3185ba40279c74200fb32732f57db8b
)

vcpkg_extract_archive(
    ARCHIVE "${MODEL_PATH}"
    DESTINATION "${SOURCE_PATH}/modeldata"
)
file(COPY "${SOURCE_PATH}/modeldata/src/rnnoise_data.c" DESTINATION "${SOURCE_PATH}/src/")
file(COPY "${SOURCE_PATH}/modeldata/src/rnnoise_data.h" DESTINATION "${SOURCE_PATH}/src/")
file(REMOVE_RECURSE "${SOURCE_PATH}/modeldata")
file(COPY "${CMAKE_CURRENT_LIST_DIR}/CMakeLists.txt" DESTINATION "${SOURCE_PATH}")
file(COPY "${CMAKE_CURRENT_LIST_DIR}/os_support.h" DESTINATION "${SOURCE_PATH}/src")

vcpkg_cmake_configure(SOURCE_PATH "${SOURCE_PATH}")
vcpkg_cmake_install()
vcpkg_cmake_config_fixup(PACKAGE_NAME rnnoise CONFIG_PATH lib/cmake/rnnoise)

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/COPYING")
