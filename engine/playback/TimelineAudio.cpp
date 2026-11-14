#include "playback/TimelineAudio.h"

#include "model/TimelineQuery.h"

#include <algorithm>
#include <cmath>
#include <thread>

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
        // Never block the audio callback, but don't mistake a lock held for a moment (the
        // producer pushing a chunk) for an empty queue: that would be a needless dropout.
        constexpr int kAttempts = 200;
        std::unique_lock lock(m_mutex, std::defer_lock);
        for (int attempt = 0; !lock.try_lock(); ++attempt) {
            if (attempt == kAttempts)
                return std::nullopt;
            std::this_thread::yield();
        }
        if (m_chunks.empty())
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

void ChannelLevels::measure(const float* stereo, int frames)
{
    double squares[2] = {0.0, 0.0};
    for (int c = 0; c < 2; ++c) {
        peak[c] = 0.0f;
        for (int i = 0; i < frames; ++i) {
            const float x = stereo[i * 2 + c];
            peak[c] = std::max(peak[c], std::abs(x));
            squares[c] += double(x) * x;
        }
        rms[c] = frames > 0 ? float(std::sqrt(squares[c] / frames)) : 0.0f;
    }
}

namespace {

bool carriesSound(const Track& track)
{
    return track.kind == TrackKind::Audio || track.kind == TrackKind::Video;
}

bool isAudible(const Project& project, const Track& track, const Clip& clip)
{
    if (track.muted || !carriesSound(track) || !clip.enabled)
        return false;
    if (track.kind == TrackKind::Video && !clip.audioEnabled)
        return false;
    if (clip.freeze || clip.generated())
        return false;
    if (clip.compound())
        return project.findSequence(clip.sequenceId) != nullptr;
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
    {
        std::lock_guard levels(m_levelsMutex);
        m_levels.clear();
    }
    const std::uint64_t generation = ++m_generation;
    // Mix on the sample grid so chunk boundaries are exact.
    const Ticks start = audio::ticksFromSamples(audio::samplesFromTicks(std::max<Ticks>(time, 0)));
    if (m_project)
        m_producer = std::thread([this, p = m_project, start, generation] { run(p, start, generation); });
}

std::optional<MixLevels> TimelineAudio::levelsAt(Ticks time) const
{
    std::lock_guard lock(m_levelsMutex);
    for (auto it = m_levels.rbegin(); it != m_levels.rend(); ++it) {
        if (it->pts <= time)
            return time < it->pts + audio::ticksFromSamples(AudioChunk::kFrames) ? std::optional(*it) : std::nullopt;
    }
    return std::nullopt;
}

Ticks TimelineAudio::tailOf(const Clip& clip)
{
    if (clip.audioEffects.empty())
        return 0;
    ClipStream& stream = m_streams[clip.id];
    if (!stream.effects)
        stream.effects = std::make_unique<AudioEffectChain>();
    stream.effects->setEffects(clip.audioEffects);
    return Ticks(stream.effects->tail() * double(kTicksPerSecond)) + audio::ticksFromSamples(stream.effects->latency());
}

void TimelineAudio::mixTracks(const Project& project, const std::vector<Track>& tracks, Ticks start, int frames,
                              Ticks keepUntil, float* mix, std::vector<float>& scratch, int depth)
{
    if (depth > kMaxNesting)
        return;
    std::vector<float>& bus = m_trackBuffers[std::size_t(depth)];
    for (const Track& track : tracks) {
        if (!carriesSound(track))
            continue;
        bus.assign(std::size_t(frames) * audio::kChannels, 0.0f);
        if (!mixTrack(project, track, start, frames, keepUntil, bus.data(), scratch, depth))
            continue;
        const float gain = track.gainDb <= kMinGainDb ? 0.0f : dsp::dbToGain(track.gainDb);
        const float left = gain * (track.pan > 0.0f ? 1.0f - track.pan : 1.0f);
        const float right = gain * (track.pan < 0.0f ? 1.0f + track.pan : 1.0f);
        for (int i = 0; i < frames; ++i) {
            mix[std::size_t(i) * 2] += bus[std::size_t(i) * 2] * left;
            mix[std::size_t(i) * 2 + 1] += bus[std::size_t(i) * 2 + 1] * right;
        }
    }
}

bool TimelineAudio::mixTrack(const Project& project, const Track& track, Ticks start, int frames, Ticks keepUntil,
                             float* bus, std::vector<float>& scratch, int depth)
{
    // Its clips over `count` samples from `from` into `dst`; whether any sounded. Clips stay
    // open until `keepUntil`, or (at the top) until their own sound and tail end.
    Ticks lastSound = 0;
    const auto mixClips = [&](Ticks from, int count, float* dst) {
        const Ticks to = from + audio::ticksFromSamples(count);
        bool any = false;
        for (const Clip& clip : track.clips) {
            if (!isAudible(project, track, clip))
                continue;
            const AudibleSpan span = audibleSpan(track, clip);
            const Ticks tail = tailOf(clip);
            const Ticks until = keepUntil >= 0 ? keepUntil : span.end + tail;
            m_clipEnds[clip.id] = std::max(m_clipEnds[clip.id], until);
            lastSound = std::max(lastSound, span.end + tail);
            if (span.end + tail <= from || span.start >= to)
                continue;
            mixClip(project, track, clip, from, count, tail, until, dst, scratch, depth);
            any = true;
        }
        return any;
    };
    if (track.audioEffects.empty())
        return mixClips(start, frames, bus);

    // Its own effects, on the clips' mix: fed that far ahead by their latency (primed after a
    // jump), ringing on after the last clip.
    TrackStream& stream = m_trackStreams[track.id];
    if (!stream.effects)
        stream.effects = std::make_unique<AudioEffectChain>();
    stream.effects->setEffects(track.audioEffects);
    const int latency = std::min(stream.effects->latency(), int(audio::kSampleRate));
    if (latency > 0 && stream.nextOutput != start) {
        std::vector<float>& prime = m_trackPrime[std::size_t(depth)];
        prime.assign(std::size_t(latency) * audio::kChannels, 0.0f);
        mixClips(start, latency, prime.data());
        stream.effects->process(prime.data(), latency);
    }
    const bool any = mixClips(start + audio::ticksFromSamples(latency), frames, bus);
    stream.nextOutput = start + audio::ticksFromSamples(frames);
    const Ticks ringing = lastSound + Ticks(stream.effects->tail() * double(kTicksPerSecond)) + audio::ticksFromSamples(latency);
    if (!any && start >= ringing)
        return false;
    stream.effects->process(bus, frames);
    return true;
}

void TimelineAudio::mixClip(const Project& project, const Track& track, const Clip& clip, Ticks chunkStart,
                            int chunkFrames, Ticks tail, Ticks keepUntil, float* mix, std::vector<float>& scratch,
                            int depth)
{
    // A clip is heard over its own span plus the handles its transitions reach into, and its
    // effects' tail after that (fed silence).
    const Ticks chunkEnd = chunkStart + audio::ticksFromSamples(chunkFrames);
    const AudibleSpan span = audibleSpan(track, clip);
    const Ticks from = std::max(chunkStart, span.start);
    const Ticks to = std::min(chunkEnd, span.end + tail);
    if (from >= to)
        return;
    ClipStream& stream = m_streams[clip.id];
    const int offset = std::min(int(audio::samplesFromTicks(from - chunkStart)), chunkFrames);
    const int frames = std::min(int(audio::samplesFromTicks(to - from)), chunkFrames - offset);
    if (frames <= 0)
        return;

    // The clip's sound (volume and fades applied) over `count` samples from `inFrom` into
    // `dst`; how many samples had sound.
    const auto renderInput = [&](Ticks inFrom, int count, float* dst) -> int {
        const Ticks inTo = inFrom + audio::ticksFromSamples(count);
        const Ticks soundUntil = std::min(inTo, span.end);
        const int soundFrames = inFrom < soundUntil ? std::min(count, int(audio::samplesFromTicks(soundUntil - inFrom))) : 0;
        int read = 0;
        const std::string enhancedPath = clip.enhancedAudio ? clip.enhancedAudio->path : std::string();
        if (stream.enhancedPath != enhancedPath) {
            stream.decoder.reset();
            stream.nextSourceTime = -1;
            stream.stretchNext = -1.0;
            stream.enhancedPath = enhancedPath;
        }
        if (soundFrames > 0 && !stream.decoder && !clip.compound()) {
            // Its cleaned sound (speech enhancement) if that file is there, else the media's own.
            if (clip.enhancedAudio)
                stream.decoder = AudioDecoder::open(clip.enhancedAudio->path);
            const MediaItem* media = project.findMedia(clip.mediaId);
            if (!stream.decoder)
                stream.decoder = media ? AudioDecoder::open(media->path) : nullptr;
        }
        const bool plainSpeed = clip.speed == 1.0 && !clip.reversed && clip.speedRamp.empty();
        // Where the sound comes from: the decoder, or a compound clip's sequence, mixed as it is
        // read (into a scratch of its own: the reads below land in `scratch`).
        const Sequence* sequence = clip.compound() ? project.findSequence(clip.sequenceId) : nullptr;
        const bool hasSource = clip.compound() ? sequence != nullptr : stream.decoder != nullptr;
        const auto seekSource = [&](Ticks sourceTime) {
            if (!sequence)
                return stream.decoder->seek(sourceTime);
            stream.sequenceAt = sourceTime;
            return true;
        };
        const auto readSource = [&](float* into, int count) {
            if (!sequence)
                return stream.decoder->read(into, count);
            std::fill_n(into, std::size_t(count) * audio::kChannels, 0.0f);
            mixTracks(project, sequence->tracks, stream.sequenceAt, count, keepUntil, into,
                      m_sequenceScratch[std::size_t(depth)], depth + 1);
            stream.sequenceAt += audio::ticksFromSamples(count);
            return count; // (silence past its end)
        };
        if (soundFrames > 0 && clip.compound() && plainSpeed) {
            // Its sequence's tracks, mixed at the time it plays.
            const Ticks sourceTime = clip.sourceTimeBeyond(inFrom);
            const int silent =
                sourceTime < 0 ? int(std::min<std::int64_t>(audio::samplesFromTicks(-sourceTime), soundFrames)) : 0;
            if (sequence && silent < soundFrames)
                mixTracks(project, sequence->tracks, std::max<Ticks>(sourceTime, 0), soundFrames - silent, keepUntil,
                          dst + std::size_t(silent) * audio::kChannels, scratch, depth + 1);
            read = soundFrames;
        } else if (soundFrames == 0 || !hasSource) {
            // In the effects' tail, or unreadable media: silence (the effects still ring dst).
        } else if (plainSpeed) {
            // Before the media's start (a head handle that is not there) is silence.
            Ticks sourceTime = clip.sourceTimeBeyond(inFrom);
            int silent = 0;
            if (sourceTime < 0) {
                silent = int(std::min<std::int64_t>(audio::samplesFromTicks(-sourceTime), soundFrames));
                sourceTime = 0;
            }
            if (silent < soundFrames) {
                if (stream.nextSourceTime == sourceTime || stream.decoder->seek(sourceTime)) {
                    read = silent
                         + stream.decoder->read(dst + std::size_t(silent) * audio::kChannels, soundFrames - silent);
                    stream.nextSourceTime = sourceTime + audio::ticksFromSamples(soundFrames - silent);
                }
            }
        } else {
            // Source samples per output sample. A ramp's speed is taken per chunk inFrom the span
            // it covers (chunks are ~21 ms).
            const Ticks soundEnd = inFrom + audio::ticksFromSamples(soundFrames);
            const Ticks sourceFrom = clip.sourceTimeBeyond(inFrom);
            const double step = clip.speedRamp.empty()
                                    ? clip.speed
                                    : double(std::llabs(clip.sourceTimeBeyond(soundEnd) - sourceFrom)) / double(soundEnd - inFrom);
            if (clip.keepPitch && !clip.reversed) {
                // Time-stretched: the same span of sound at the clip's speed, at its own pitch.
                // The stretcher runs on inFrom chunk to chunk; a jump (seek, edit) restarts it.
                const double position = double(sourceFrom) * audio::kSampleRate / double(kTicksPerSecond);
                if (!stream.stretcher)
                    stream.stretcher = std::make_unique<dsp::TimeStretcher>();
                dsp::TimeStretcher& stretcher = *stream.stretcher;
                if (stream.stretchNext < 0.0 || std::abs(position - stream.stretchNext) > 64.0) {
                    const auto inputStart = std::max<std::int64_t>(
                        0, std::int64_t(position - (2.0 * step + 1.5) * dsp::TimeStretcher::kHop - dsp::TimeStretcher::kSearch));
                    if (!seekSource(audio::ticksFromSamples(inputStart)))
                        return read;
                    stretcher.reset(inputStart, position, step);
                    stream.nextSourceTime = -1;
                }
                scratch.resize(std::size_t(1024) * audio::kChannels);
                while (!stretcher.canPull(soundFrames, step)) {
                    const int got = readSource(scratch.data(), 1024);
                    if (got <= 0) {
                        stretcher.finish();
                        break;
                    }
                    stretcher.push(scratch.data(), got);
                }
                stretcher.pull(dst, soundFrames, step);
                stream.stretchNext = position + soundFrames * step;
                read = soundFrames;
            } else {
                // Resampled (pitch follows speed, like tape). Reversed clips read the span and
                // walk it backwards.
                const int needed = int(std::ceil(double(soundFrames) * step)) + 2;
                const Ticks spanTicks = Ticks(std::llround(double(soundEnd - inFrom) * step));
                const Ticks blockStart = std::max<Ticks>(0, clip.reversed ? sourceFrom + 1 - spanTicks : sourceFrom);
                if (!seekSource(blockStart))
                    return read;
                stream.nextSourceTime = -1; // resampled reads always seek
                scratch.assign(std::size_t(needed) * audio::kChannels, 0.0f);
                const int available = readSource(scratch.data(), needed);
                if (available >= 2) {
                    const double top =
                        clip.reversed ? double(audio::samplesFromTicks(sourceFrom + 1 - blockStart)) - 1.0 : 0.0;
                    for (int i = 0; i < soundFrames; ++i) {
                        double position = clip.reversed ? top - double(i) * step : double(i) * step;
                        position = std::clamp(position, 0.0, double(available - 1));
                        const int i0 = std::min(int(position), available - 2);
                        const float t = float(position - double(i0));
                        for (int c = 0; c < audio::kChannels; ++c) {
                            const float a = scratch[std::size_t(i0) * 2 + std::size_t(c)];
                            const float b = scratch[std::size_t(i0 + 1) * 2 + std::size_t(c)];
                            dst[std::size_t(i) * 2 + std::size_t(c)] = a + (b - a) * t;
                        }
                    }
                    read = soundFrames;
                }
            }
        }

        // Fades are linear, so interpolating the gain across the span is exact; transition
        // crossfades are curves, close enough to linear over one chunk. Fades apply inside the
        // clip and hold their edge value in the handles.
        if (read > 0) {
            const auto gainAt = [&](Ticks time) {
                return clip.gainAt(std::clamp(time, clip.start, clip.end())) * transitionGainAt(track, clip, time);
            };
            const Ticks readEnd = inFrom + audio::ticksFromSamples(read);
            const float gainFrom = gainAt(inFrom);
            const float gainStep = read > 1 ? (gainAt(readEnd) - gainFrom) / float(read) : 0.0f;
            for (int i = 0; i < read; ++i) {
                const float gain = gainFrom + gainStep * float(i);
                dst[std::size_t(i) * 2] *= gain;
                dst[std::size_t(i) * 2 + 1] *= gain;
            }
        }
        return read;
    };

    // Then the clip's effects (animated values as at this chunk), which run on past the end
    // of its sound. Plug-ins that delay their output are fed the sound that far ahead, and
    // primed with what comes before it after a jump, so what comes out lines up.
    std::vector<float>& out = m_clipBuffers[std::size_t(depth)];
    out.assign(std::size_t(frames) * audio::kChannels, 0.0f);
    int latency = 0;
    if (!clip.audioEffects.empty()) {
        if (!stream.effects)
            stream.effects = std::make_unique<AudioEffectChain>();
        stream.effects->setEffects(clip.audioEffectsAt(from));
        latency = std::min(stream.effects->latency(), int(audio::kSampleRate));
        if (latency > 0 && stream.nextOutput != from) {
            std::vector<float>& prime = m_clipPrime[std::size_t(depth)];
            prime.assign(std::size_t(latency) * audio::kChannels, 0.0f);
            renderInput(from, latency, prime.data());
            stream.effects->process(prime.data(), latency);
        }
    }
    const int read = renderInput(from + audio::ticksFromSamples(latency), frames, out.data());
    stream.nextOutput = from + audio::ticksFromSamples(frames);
    if (stream.effects && !clip.audioEffects.empty())
        stream.effects->process(out.data(), frames);
    else if (read == 0)
        return;
    float* target = mix + offset * audio::kChannels;
    for (std::size_t i = 0; i < std::size_t(frames) * 2; ++i)
        target[i] += out[i];
}

void TimelineAudio::run(std::shared_ptr<const Project> project, Ticks start, std::uint64_t generation)
{
    const Ticks end = project->duration();
    const Ticks chunkTicks = audio::ticksFromSamples(AudioChunk::kFrames);
    std::vector<float> scratch;
    std::vector<float> bus(std::size_t(AudioChunk::kFrames) * audio::kChannels);
    // Effects and stretchers start fresh: what they held belongs to wherever play was before.
    for (auto& [id, stream] : m_trackStreams) {
        if (stream.effects)
            stream.effects->reset();
        stream.nextOutput = -1;
    }
    for (auto& [id, stream] : m_streams) {
        stream.nextOutput = -1;
        if (stream.effects)
            stream.effects->reset();
        stream.stretchNext = -1.0;
        stream.nextSourceTime = -1;
    }
    m_meter.reset();
    const bool anySolo = std::ranges::any_of(project->tracks, [](const Track& t) { return t.solo && carriesSound(t); });

    for (Ticks time = start; !m_stop && time < end; time += chunkTicks) {
        AudioChunk chunk{time, generation, std::vector<float>(AudioChunk::kFrames * audio::kChannels, 0.0f)};
        MixLevels levels;
        levels.pts = time;
        m_clipEnds.clear();
        for (const Track& track : project->tracks) {
            if (!carriesSound(track))
                continue;
            ChannelLevels trackLevels;
            if (!anySolo || track.solo) {
                std::ranges::fill(bus, 0.0f);
                if (mixTrack(*project, track, time, AudioChunk::kFrames, -1, bus.data(), scratch, 0)) {
                    // The track's volume, then its balance (unity in the middle).
                    const float gain = track.gainDb <= kMinGainDb ? 0.0f : dsp::dbToGain(track.gainDb);
                    const float left = gain * (track.pan > 0.0f ? 1.0f - track.pan : 1.0f);
                    const float right = gain * (track.pan < 0.0f ? 1.0f + track.pan : 1.0f);
                    for (int i = 0; i < AudioChunk::kFrames; ++i) {
                        bus[std::size_t(i) * 2] *= left;
                        bus[std::size_t(i) * 2 + 1] *= right;
                    }
                    trackLevels.measure(bus.data(), AudioChunk::kFrames);
                    for (std::size_t i = 0; i < bus.size(); ++i)
                        chunk.samples[i] += bus[i];
                }
            }
            levels.tracks.emplace_back(track.id, trackLevels);
        }
        levels.master.measure(chunk.samples.data(), AudioChunk::kFrames);
        m_meter.process(chunk.samples.data(), AudioChunk::kFrames);
        levels.momentary = m_meter.momentary();
        levels.shortTerm = m_meter.shortTerm();
        levels.integrated = m_meter.integrated();
        {
            std::lock_guard lock(m_levelsMutex);
            m_levels.push_back(std::move(levels));
            while (m_levels.size() > 400) // ~8 s
                m_levels.pop_front();
        }
        // Close decoders of clips that are behind us.
        std::erase_if(m_streams, [&](const auto& entry) {
            const auto clipEnd = m_clipEnds.find(entry.first);
            return clipEnd == m_clipEnds.end() || clipEnd->second < time;
        });
        if (!m_queue.push(std::move(chunk)))
            return;
    }
    m_finished = true;
}

double measureLoudness(std::shared_ptr<const Project> project, Ticks from, Ticks to)
{
    TimelineAudio mixer;
    mixer.restart(std::move(project), from);
    dsp::LoudnessMeter meter;
    while (true) {
        if (auto chunk = mixer.queue().tryPop()) {
            if (chunk->pts >= to)
                break;
            const int frames = int(std::min<std::int64_t>(AudioChunk::kFrames, audio::samplesFromTicks(to - chunk->pts)));
            meter.process(chunk->samples.data(), frames);
            continue;
        }
        if (mixer.producerFinished() && mixer.queue().size() == 0)
            break;
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
    return meter.integrated();
}

} // namespace tve
