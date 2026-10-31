add_library(tve_engine STATIC
    decode/DecodedFrame.h
    decode/VideoDecoder.h
    decode/VideoDecoder.cpp
    decode/AudioDecoder.h
    decode/AudioDecoder.cpp
    decode/Thumbnail.h
    decode/Thumbnail.cpp
    decode/LottieAnimation.h
    decode/LottieAnimation.cpp
    encode/MediaWriter.h
    encode/MediaWriter.cpp
    encode/GifPalette.h
    encode/GifPalette.cpp
    encode/Proxy.h
    encode/Proxy.cpp
    encode/WavWriter.h
    encode/WavWriter.cpp
    encode/VideoProcessing.h
    encode/VideoProcessing.mm
    playback/FrameQueue.h
    playback/FrameSource.h
    playback/PlaybackClock.h
    playback/PlaybackSession.h
    playback/PlaybackSession.cpp
    playback/TimelinePlayback.h
    playback/TimelinePlayback.cpp
    playback/TimelineAudio.h
    playback/TimelineAudio.cpp
    audio/AudioOutput.h
    audio/AudioOutput.cpp
    audio/AudioRecorder.h
    audio/AudioRecorder.cpp
    audio/MiniaudioImpl.c
    analysis/Scopes.h
    analysis/Scopes.cpp
    analysis/MediaAnalysis.h
    analysis/MediaAnalysis.cpp
    analysis/PeopleVision.h
    analysis/MotionAnalysis.h
    analysis/MotionAnalysis.cpp
    analysis/Tracking.h
    analysis/Tracking.mm
    analysis/PeopleVision.mm
    speech/Transcriber.h
    speech/Transcriber.cpp
    speech/SpeechEnhancer.h
    speech/SpeechEnhancer.cpp
    audio/WaveformCache.h
    audio/WaveformCache.cpp
    audio/AudioEffects.h
    audio/AudioEffects.cpp
    audio/dsp/Biquad.h
    audio/dsp/Processors.h
    audio/dsp/Processors.cpp
    audio/dsp/Denoise.h
    audio/dsp/Denoise.cpp
    audio/dsp/TimeStretch.h
    audio/dsp/TimeStretch.cpp
    audio/dsp/Loudness.h
    audio/dsp/Loudness.cpp
)

# The audio effect packages (effects/audio/<folder>/manifest.json) are compiled in as text:
# the engine has no Qt resources. Editing a manifest re-runs this at the next build.
file(GLOB TVE_AUDIO_EFFECT_MANIFESTS CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/effects/audio/*/manifest.json)
set(TVE_AUDIO_EFFECT_ENTRIES "")
foreach(manifest ${TVE_AUDIO_EFFECT_MANIFESTS})
    get_filename_component(folder ${manifest} DIRECTORY)
    get_filename_component(folder ${folder} NAME)
    file(READ ${manifest} json)
    string(APPEND TVE_AUDIO_EFFECT_ENTRIES "        {\"${folder}\", R\"tvejson(${json})tvejson\"},\n")
endforeach()
configure_file(audio/BuiltinAudioEffects.cpp.in ${CMAKE_CURRENT_BINARY_DIR}/BuiltinAudioEffects.cpp @ONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${TVE_AUDIO_EFFECT_MANIFESTS})
target_sources(tve_engine PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/BuiltinAudioEffects.cpp)
set_source_files_properties(audio/MiniaudioImpl.c PROPERTIES COMPILE_OPTIONS "-w")

# ThorVG (MIT) draws Lottie stickers; vcpkg builds it as a static library with its Lottie loader.
find_path(THORVG_INCLUDE_DIR thorvg.h PATH_SUFFIXES thorvg-1 REQUIRED)
find_library(THORVG_LIBRARY thorvg-1 REQUIRED)

# whisper.cpp (MIT) on ggml (MIT, with its Metal backend) for speech to text.
find_package(whisper CONFIG REQUIRED)

# RNNoise (BSD-3-Clause, model data CC0) for live voice noise reduction, and ONNX Runtime
# (MIT) running DeepFilterNet3 (MIT / Apache-2.0) for speech enhancement.
find_package(rnnoise CONFIG REQUIRED)
find_package(onnxruntime CONFIG REQUIRED)

# miniaudio is a single header from vcpkg.
find_path(MINIAUDIO_INCLUDE_DIR miniaudio.h REQUIRED)
add_library(tve::engine ALIAS tve_engine)

target_include_directories(tve_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(tve_engine SYSTEM PRIVATE ${MINIAUDIO_INCLUDE_DIR} ${THORVG_INCLUDE_DIR})
target_link_libraries(tve_engine
    PUBLIC
        tve::core
        "-framework CoreVideo"
        "-framework CoreFoundation"
    PRIVATE
        tve::ffmpeg
        ${THORVG_LIBRARY}
        whisper
        rnnoise::rnnoise
        onnxruntime::onnxruntime
        "-framework Accelerate"
        "-framework Foundation"
        "-framework Vision"
        "-framework CoreGraphics"
        nlohmann_json::nlohmann_json
        "-framework VideoToolbox"
        "-framework CoreMedia"
        "-framework CoreAudio"
        "-framework AudioToolbox"
)
tve_set_warnings(tve_engine)
