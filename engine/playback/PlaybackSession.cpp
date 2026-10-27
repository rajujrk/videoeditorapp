#include "playback/PlaybackSession.h"

#include <algorithm>

namespace tve {

std::shared_ptr<PlaybackSession> PlaybackSession::open(const std::string& path, std::string* error)
{
    auto decoder = VideoDecoder::open(path, error);
    if (!decoder)
        return nullptr;
    return std::shared_ptr<PlaybackSession>(new PlaybackSession(std::move(decoder)));
}

PlaybackSession::PlaybackSession(std::unique_ptr<VideoDecoder> decoder)
    : m_decoder(std::move(decoder)), m_info(m_decoder->info())
{
    m_hardware = m_info.hardwareAccelerated;
}

PlaybackSession::~PlaybackSession()
{
    stop();
}

void PlaybackSession::start()
{
    if (m_thread.joinable())
        return;
    m_stopRequested = false;
    m_thread = std::thread([this] { run(); });
}

void PlaybackSession::stop()
{
    m_stopRequested = true;
    m_queue.stop();
    if (m_thread.joinable())
        m_thread.join();
}

VideoStreamInfo PlaybackSession::info() const
{
    VideoStreamInfo info = m_info;
    info.hardwareAccelerated = m_hardware;
    return info;
}

void PlaybackSession::run()
{
    const Ticks frameTicks = frameDuration(m_info.frameRate);
    Ticks loopOffset = 0;
    Ticks loopEnd = 0;
    bool decodedThisLoop = false;

    while (!m_stopRequested) {
        auto frame = m_decoder->nextFrame();
        if (!frame) {
            // End of file: restart, continuing the timeline after the last frame.
            if (!decodedThisLoop || !m_decoder->seekToStart())
                break;
            loopOffset = loopEnd;
            decodedThisLoop = false;
            continue;
        }
        decodedThisLoop = true;
        m_hardware = m_decoder->info().hardwareAccelerated;
        frame->setPts(frame->pts() + loopOffset);
        loopEnd = std::max(loopEnd, frame->pts() + frameTicks);
        ++m_counters.decoded;
        if (!m_queue.push(std::move(*frame)))
            break;
    }
}

} // namespace tve
