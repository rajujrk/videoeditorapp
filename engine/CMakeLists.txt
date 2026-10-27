add_library(tve_engine STATIC
    decode/DecodedFrame.h
    decode/VideoDecoder.h
    decode/VideoDecoder.cpp
    playback/FrameQueue.h
    playback/FrameSource.h
    playback/PlaybackClock.h
    playback/PlaybackSession.h
    playback/PlaybackSession.cpp
    playback/TimelinePlayback.h
    playback/TimelinePlayback.cpp
)
add_library(tve::engine ALIAS tve_engine)

target_include_directories(tve_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(tve_engine
    PUBLIC
        tve::core
        "-framework CoreVideo"
        "-framework CoreFoundation"
    PRIVATE
        tve::ffmpeg
        "-framework VideoToolbox"
        "-framework CoreMedia"
)
tve_set_warnings(tve_engine)
