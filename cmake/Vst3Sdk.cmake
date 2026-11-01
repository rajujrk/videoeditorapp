# The VST3 SDK's hosting parts (MIT licence; Steinberg, v3.8.1): pluginterfaces, base and
# public.sdk, fetched at configure time into build/_deps/vst3sdk/ and checked against their
# SHA-512 (the same archives and hashes as vcpkg's vst3sdk port). Only the files a host
# needs are compiled (engine/CMakeLists.txt); nothing of the SDK is kept in the repository.
include(FetchContent)

set(TVE_VST3_TAG v3.8.1_build_84)
set(TVE_VST3_ROOT ${CMAKE_BINARY_DIR}/_deps/vst3sdk)

FetchContent_Declare(vst3_pluginterfaces
    URL https://github.com/steinbergmedia/vst3_pluginterfaces/archive/refs/tags/${TVE_VST3_TAG}.tar.gz
    URL_HASH SHA512=fd2218c3e949133dd51b4834c6680131a520587c4449fa50f9b86cb3de3c3091e34ff15e21ba0e995f078b43890e7aa239a1beeb753eba1ce6822f9b44875b3c
    SOURCE_DIR ${TVE_VST3_ROOT}/pluginterfaces
    SOURCE_SUBDIR not-built)
FetchContent_Declare(vst3_base
    URL https://github.com/steinbergmedia/vst3_base/archive/refs/tags/${TVE_VST3_TAG}.tar.gz
    URL_HASH SHA512=12de902576171233e382e0544cdfc8a651d6f640038569eb760ae1d3fe0fd43360952d1dcac209dd4224a9d6a669ce2ff41f9ba5279c35b8cf9622acd74d0857
    SOURCE_DIR ${TVE_VST3_ROOT}/base
    SOURCE_SUBDIR not-built)
FetchContent_Declare(vst3_public_sdk
    URL https://github.com/steinbergmedia/vst3_public_sdk/archive/refs/tags/${TVE_VST3_TAG}.tar.gz
    URL_HASH SHA512=d902a7b3f0d925e95d343df2378f4b701309d6460c970dfa064f0c2f24b0b4ca700ea15b3a4b63b3fc5f38cc5d6b2d55e5a3946fd10dd45960a86f948c868552
    SOURCE_DIR ${TVE_VST3_ROOT}/public.sdk
    SOURCE_SUBDIR not-built)
FetchContent_MakeAvailable(vst3_pluginterfaces vst3_base vst3_public_sdk)

# What hosts and plug-ins both need (interfaces, base classes, streams).
set(_vst3 ${TVE_VST3_ROOT})
add_library(tve_vst3_sdk STATIC
    ${_vst3}/pluginterfaces/base/conststringtable.cpp
    ${_vst3}/pluginterfaces/base/coreiids.cpp
    ${_vst3}/pluginterfaces/base/funknown.cpp
    ${_vst3}/pluginterfaces/base/ustring.cpp
    ${_vst3}/base/source/baseiids.cpp
    ${_vst3}/base/source/fbuffer.cpp
    ${_vst3}/base/source/fdebug.cpp
    ${_vst3}/base/source/fobject.cpp
    ${_vst3}/base/source/fstreamer.cpp
    ${_vst3}/base/source/fstring.cpp
    ${_vst3}/base/source/timer.cpp
    ${_vst3}/base/source/updatehandler.cpp
    ${_vst3}/base/thread/source/fcondition.cpp
    ${_vst3}/base/thread/source/flock.cpp
    ${_vst3}/public.sdk/source/common/commoniids.cpp
    ${_vst3}/public.sdk/source/common/commonstringconvert.cpp
    ${_vst3}/public.sdk/source/common/memorystream.cpp
    ${_vst3}/public.sdk/source/common/threadchecker_mac.mm
    ${_vst3}/public.sdk/source/vst/vstinitiids.cpp
    ${_vst3}/public.sdk/source/vst/utility/stringconvert.cpp)
target_include_directories(tve_vst3_sdk SYSTEM PUBLIC ${_vst3})
target_compile_definitions(tve_vst3_sdk PUBLIC $<IF:$<CONFIG:Debug>,DEVELOPMENT=1,RELEASE=1>)
target_compile_options(tve_vst3_sdk PRIVATE -w)
target_link_libraries(tve_vst3_sdk PUBLIC "-framework CoreFoundation" "-framework Foundation")

# A host's helpers: loading modules, plug-in providers, process data, parameter changes.
add_library(tve_vst3_hosting STATIC
    ${_vst3}/public.sdk/source/vst/hosting/connectionproxy.cpp
    ${_vst3}/public.sdk/source/vst/hosting/eventlist.cpp
    ${_vst3}/public.sdk/source/vst/hosting/hostclasses.cpp
    ${_vst3}/public.sdk/source/vst/hosting/module.cpp
    ${_vst3}/public.sdk/source/vst/hosting/module_mac.mm
    ${_vst3}/public.sdk/source/vst/hosting/parameterchanges.cpp
    ${_vst3}/public.sdk/source/vst/hosting/pluginterfacesupport.cpp
    ${_vst3}/public.sdk/source/vst/hosting/plugprovider.cpp
    ${_vst3}/public.sdk/source/vst/hosting/processdata.cpp)
target_compile_options(tve_vst3_hosting PRIVATE -w -fobjc-arc)
target_link_libraries(tve_vst3_hosting PUBLIC tve_vst3_sdk)

# A plug-in's side, for the test plug-in.
add_library(tve_vst3_plugin STATIC
    ${_vst3}/public.sdk/source/main/pluginfactory.cpp
    ${_vst3}/public.sdk/source/vst/vstbus.cpp
    ${_vst3}/public.sdk/source/vst/vstcomponentbase.cpp
    ${_vst3}/public.sdk/source/vst/vstparameters.cpp
    ${_vst3}/public.sdk/source/vst/vstsinglecomponenteffect.cpp
    ${_vst3}/public.sdk/source/vst/vsteditcontroller.cpp
    ${_vst3}/public.sdk/source/common/pluginview.cpp)
target_compile_options(tve_vst3_plugin PRIVATE -w)
target_link_libraries(tve_vst3_plugin PUBLIC tve_vst3_sdk)
# A plug-in bundle's entry points (compiled into the bundle itself, so they're exported).
set(TVE_VST3_PLUGIN_ENTRY ${_vst3}/public.sdk/source/main/macmain.cpp)
