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
