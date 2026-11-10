extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libswscale/swscale.h>
}

#include <catch2/catch_test_macros.hpp>

#include <string>

// TVE links FFmpeg into a closed-source app, so every FFmpeg library must be an LGPL
// build. A GPL build (e.g. --enable-gpl for x264) reports "GPL version 2 or later".
TEST_CASE("linked FFmpeg libraries are LGPL builds")
{
    for (const char* license : {avutil_license(), avcodec_license(), avformat_license(), swscale_license()}) {
        INFO(license);
        CHECK(std::string(license).starts_with("LGPL"));
    }
}

TEST_CASE("FFmpeg was built with VideoToolbox")
{
    CHECK(std::string(avcodec_configuration()).find("--enable-videotoolbox") != std::string::npos);
}

// MP3 export: LAME (LGPL, like FFmpeg here), and with it no GPL or non-free parts.
TEST_CASE("FFmpeg was built with LAME, and nothing GPL or non-free")
{
    const std::string configuration = avcodec_configuration();
    CHECK(configuration.find("--enable-libmp3lame") != std::string::npos);
    CHECK(configuration.find("--enable-gpl") == std::string::npos);
    CHECK(configuration.find("--enable-nonfree") == std::string::npos);
    CHECK(avcodec_find_encoder_by_name("libmp3lame"));
}
