#include "playback/TimelineAudio.h"

#include <algorithm>
#include <cmath>

namespace tve {

// --- AudioChunkQueue ---

bool AudioChunkQueue::push(AudioChunk chunk)
{
    std::unique_lock lock(m_mutex);
    m_notFull.wait(lock, [&] { return m_stopped || m_chunks.size() < m_capacity; });
    if (m_stopped)
        return false;
    m_chunks.push_back(std::move(chunk));
    return true;
}

std::optional<AudioChunk> AudioChunkQueue::tryPop()
{
    std::optional<AudioChunk> chunk;
    {
        std::unique_lock lock(m_mutex, std::try_to_lock);
        if (!lock.owns_lock() || m_chunks.empty())
            return std::nullopt;
        chunk = std::move(m_chunks.front());
        m_chunks.pop_front();
    }
    m_notFull.notify_one();
    return chunk;
}

void AudioChunkQueue::stop()
{
    {
        std::lock_guard lock(m_mutex);
        m_stopped = true;
        m_chunks.clear();
    }
    m_notFull.notify_all();
}

void AudioChunkQueue::restart()
{
    std::lock_guard lock(m_mutex);
    m_chunks.clear();
    m_stopped = false;
}

std::size_t AudioChunkQueue::size() const
{
    std::lock_guard lock(m_mutex);
    return m_chunks.size();
}

// --- TimelineAudio ---

namespace {

bool isAudible(const Project& project, const Track& track, const Clip& clip)
{
    if (track.muted)
        return false;
    if (track.kind == TrackKind::Video && !clip.audioEnabled)
        return false;
    if (track.kind == TrackKind::Title || clip.freeze)
        return false;
    const MediaItem* media = project.findMedia(clip.mediaId);
    return media && media->hasAudio;
}

} // namespace

TimelineAudio::~TimelineAudio()
{
    stopProducer();
}

void TimelineAudio::stopProducer()
{
    m_stop = true;
    m_queue.stop();
    if (m_producer.joinable())
        m_producer.join();
}

void TimelineAudio::restart(std::shared_ptr<const Project> project, Ticks time)
{
    std::lock_guard lock(m_control);
    stopProducer();
    if (project)
        m_project = std::move(project);
    m_queue.restart();
    m_stop = false;
    m_finished = false;
    const std::uint64_t generation = ++m_generation;
    // Mix on the sample grid so chunk boundaries are exact.
    const Ticks start = audio::ticksFromSamples(audio::samplesFromTicks(std::max<Ticks>(time, 0)));
    if (m_project)
        m_producer = std::thread([this, p = m_project, start, generation] { run(p, start, generation); });
}

void TimelineAudio::mixClip(const Project& project, const Clip& clip, Ticks chunkStart, float* mix,
                            std::vector<float>& scratch)
{
    const Ticks chunkEnd = chunkStart + audio::ticksFromSamples(AudioChunk::kFrames);
    const Ticks from = std::max(chunkStart, clip.start);
    const Ticks to = std::min(chunkEnd, clip.end());
    if (from >= to)
        return;

    ClipStream& stream = m_streams[clip.id];
    if (!stream.decoder) {
        const MediaItem* media = project.findMedia(clip.mediaId);
        stream.decoder = media ? AudioDecoder::open(media->path) : nullptr;
        if (!stream.decoder)
            return; // missing or unreadable media is silent
    }
    const int offset = int(audio::samplesFromTicks(from - chunkStart));
    const int frames = int(audio::samplesFromTicks(to - from));
    const bool plainSpeed = clip.speed == 1.0 && !clip.reversed;
    int read = 0;
    std::vector<float>& out = m_resampled;
    out.assign(std::size_t(frames) * audio::kChannels, 0.0f);

    if (plainSpeed) {
        const Ticks sourceTime = clip.sourceTimeAt(from);
        if (stream.nextSourceTime != sourceTime && !stream.decoder->seek(sourceTime))
            return;
        read = stream.decoder->read(out.data(), frames);
        stream.nextSourceTime = sourceTime + audio::ticksFromSamples(frames);
    } else {
        // Read the source span these output samples cover and resample it (pitch follows
        // speed, like tape). Reversed clips read the span and walk it backwards.
        const double step = clip.speed;
        const int needed = int(std::ceil(double(frames) * step)) + 2;
        const Ticks spanTicks = Ticks(std::llround(double(to - from) * step));
        const Ticks blockStart = clip.reversed ? std::max(clip.sourceIn, clip.sourceTimeAt(from) + 1 - spanTicks)
                                               : clip.sourceTimeAt(from);
        if (!stream.decoder->seek(blockStart))
            return;
        stream.nextSourceTime = -1; // resampled reads always seek
        scratch.assign(std::size_t(needed) * audio::kChannels, 0.0f);
        const int available = stream.decoder->read(scratch.data(), needed);
        if (available < 2)
            return;
        const double top = clip.reversed ? double(audio::samplesFromTicks(clip.sourceTimeAt(from) + 1 - blockStart)) - 1.0
                                         : 0.0;
        for (int i = 0; i < frames; ++i) {
            double position = clip.reversed ? top - double(i) * step : double(i) * step;
            position = std::clamp(position, 0.0, double(available - 1));
            const int i0 = std::min(int(position), available - 2);
            const float t = float(position - double(i0));
            for (int c = 0; c < audio::kChannels; ++c) {
                const float a = scratch[std::size_t(i0) * 2 + std::size_t(c)];
                const float b = scratch[std::size_t(i0 + 1) * 2 + std::size_t(c)];
                out[std::size_t(i) * 2 + std::size_t(c)] = a + (b - a) * t;
            }
        }
        read = frames;
    }

    // Fades are linear, so interpolating the gain across the span is exact.
    const float gainFrom = clip.gainAt(from);
    const float gainTo = clip.gainAt(to);
    const float gainStep = frames > 1 ? (gainTo - gainFrom) / float(frames) : 0.0f;
    float* target = mix + offset * audio::kChannels;
    for (int i = 0; i < read; ++i) {
        const float gain = gainFrom + gainStep * float(i);
        target[i * 2] += out[std::size_t(i) * 2] * gain;
        target[i * 2 + 1] += out[std::size_t(i) * 2 + 1] * gain;
    }
}

void TimelineAudio::run(std::shared_ptr<const Project> project, Ticks start, std::uint64_t generation)
{
    const Ticks end = project->duration();
    const Ticks chunkTicks = audio::ticksFromSamples(AudioChunk::kFrames);
    std::vector<float> scratch;
    std::map<Id, Ticks> clipEnds;
    for (const Track& track : project->tracks) {
        for (const Clip& clip : track.clips)
            clipEnds[clip.id] = clip.end();
    }

    for (Ticks time = start; !m_stop && time < end; time += chunkTicks) {
        AudioChunk chunk{time, generation, std::vector<float>(AudioChunk::kFrames * audio::kChannels, 0.0f)};
        for (const Track& track : project->tracks) {
            for (const Clip& clip : track.clips) {
                if (clip.end() <= time || clip.start >= chunk.end() || !isAudible(*project, track, clip))
                    continue;
                mixClip(*project, clip, time, chunk.samples.data(), scratch);
            }
        }
        // Close decoders of clips that are behind us.
        std::erase_if(m_streams, [&](const auto& entry) {
            const auto clipEnd = clipEnds.find(entry.first);
            return clipEnd == clipEnds.end() || clipEnd->second < time;
        });
        if (!m_queue.push(std::move(chunk)))
            return;
    }
    m_finished = true;
}

} // namespace tve
