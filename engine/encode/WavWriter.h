#pragma once

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>

namespace tve {

// Writes a WAV file of 32-bit float samples. The header's sizes are brought up to date on
// every flush(), so a file cut short by a crash still opens with everything flushed so far.
// Not thread-safe.
class WavWriter {
public:
    ~WavWriter();
    WavWriter(const WavWriter&) = delete;
    WavWriter& operator=(const WavWriter&) = delete;

    static std::unique_ptr<WavWriter> open(const std::string& path, int sampleRate, int channels,
                                           std::string* error = nullptr);

    // Appends interleaved frames (channels() floats each).
    bool write(const float* samples, std::int64_t frames);
    // Writes the header's sizes and pushes buffered data to the file.
    bool flush();
    // Flushes and closes the file. Later calls do nothing.
    bool finish();

    int sampleRate() const { return m_sampleRate; }
    int channels() const { return m_channels; }
    std::int64_t frames() const { return m_frames; }

private:
    WavWriter(std::FILE* file, int sampleRate, int channels);
    bool writeHeader();

    std::FILE* m_file;
    int m_sampleRate;
    int m_channels;
    std::int64_t m_frames = 0;
    bool m_failed = false;
};

} // namespace tve
