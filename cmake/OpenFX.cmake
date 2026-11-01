# The OpenFX API (BSD-3-Clause; Academy Software Foundation, release 1.5.1), fetched at
# configure time into build/_deps/openfx/ and checked against its SHA-512 (taken when the
# release was first downloaded for slice 19b). Only its headers are used: the host is ours
# (engine/video/OfxHost.mm).
include(FetchContent)

FetchContent_Declare(openfx
    URL https://github.com/AcademySoftwareFoundation/openfx/archive/refs/tags/OFX_Release_1.5.1.tar.gz
    URL_HASH SHA512=c4411e49759bd859a95ba9b8b836ebdb2bbc185b8767fa378019a9e34532e7cba3a146e27e781ae5b1d8d3ec5b12f8d58b602590c15069d9211c0743f93344af
    SOURCE_DIR ${CMAKE_BINARY_DIR}/_deps/openfx
    SOURCE_SUBDIR not-built)
FetchContent_MakeAvailable(openfx)

add_library(tve_openfx_api INTERFACE)
target_include_directories(tve_openfx_api SYSTEM INTERFACE ${CMAKE_BINARY_DIR}/_deps/openfx/include)
