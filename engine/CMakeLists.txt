add_library(tve_engine STATIC
    decode/DecodedFrame.h
    decode/VideoDecoder.h
    decode/VideoDecoder.cpp
    decode/AudioDecoder.h
    decode/AudioDecoder.cpp
    decode/Thumbnail.h
    decode/Thumbnail.cpp
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
    audio/MiniaudioImpl.c
    audio/WaveformCache.h
    audio/WaveformCache.cpp
)
set_source_files_properties(audio/MiniaudioImpl.c PROPERTIES COMPILE_OPTIONS "-w")

# miniaudio is a single header from vcpkg.
find_path(MINIAUDIO_INCLUDE_DIR miniaudio.h REQUIRED)
add_library(tve::engine ALIAS tve_engine)

target_include_directories(tve_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(tve_engine SYSTEM PRIVATE ${MINIAUDIO_INCLUDE_DIR})
target_link_libraries(tve_engine
    PUBLIC
        tve::core
        "-framework CoreVideo"
        "-framework CoreFoundation"
    PRIVATE
        tve::ffmpeg
        "-framework VideoToolbox"
        "-framework CoreMedia"
        "-framework CoreAudio"
        "-framework AudioToolbox"
)
tve_set_warnings(tve_engine)
