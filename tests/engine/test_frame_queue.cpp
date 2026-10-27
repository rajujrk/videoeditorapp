#include "decode/DecodedFrame.h"
#include "playback/FrameQueue.h"

#include <catch2/catch_test_macros.hpp>

#include <thread>

using namespace tve;

namespace {

DecodedFrame makeFrame(Ticks pts)
{
    CVPixelBufferRef buffer = nullptr;
    CVPixelBufferCreate(nullptr, 16, 16, kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange, nullptr, &buffer);
    return DecodedFrame(buffer, pts);
}

} // namespace

TEST_CASE("takeLatestDue returns the newest due frame and counts skipped ones")
{
    BasicFrameQueue<DecodedFrame> queue(8);
    for (Ticks pts : {0, 10, 20, 30})
        REQUIRE(queue.push(makeFrame(pts)));

    int skipped = 0;
    CHECK_FALSE(queue.takeLatestDue(-1, skipped));

    auto frame = queue.takeLatestDue(25, skipped);
    REQUIRE(frame);
    CHECK(frame->pts() == 20);
    CHECK(skipped == 2);
    CHECK(queue.frontPts() == 30);
}

TEST_CASE("stop unblocks a producer waiting on a full queue")
{
    BasicFrameQueue<DecodedFrame> queue(1);
    REQUIRE(queue.push(makeFrame(0)));

    bool pushed = true;
    std::thread producer([&] { pushed = queue.push(makeFrame(1)); });
    queue.stop();
    producer.join();
    CHECK_FALSE(pushed);
}
