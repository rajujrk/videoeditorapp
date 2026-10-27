#include "encode/WavWriter.h"

#include <array>
#include <cerrno>
#include <cstring>
#include <limits>

namespace tve {

namespace {

constexpr std::uint16_t kFormatIeeeFloat = 3;
constexpr int kBytesPerSample = 4;
// RIFF header (12) + fmt chunk (8 + 18) + fact chunk (8 + 4) + data chunk header (8).
constexpr std::size_t kHeaderSize = 58;

void put16(std::uint8_t*& p, std::uint16_t v)
{
    *p++ = std::uint8_t(v);
    *p++ = std::uint8_t(v >> 8);
}

void put32(std::uint8_t*& p, std::uint32_t v)
{
    for (int i = 0; i < 4; ++i)
        *p++ = std::uint8_t(v >> (8 * i));
}

void putTag(std::uint8_t*& p, const char (&tag)[5])
{
    std::memcpy(p, tag, 4);
    p += 4;
}

} // namespace

WavWriter::WavWriter(std::FILE* file, int sampleRate, int channels)
    : m_file(file), m_sampleRate(sampleRate), m_channels(channels)
{
}

WavWriter::~WavWriter()
{
    finish();
}

std::unique_ptr<WavWriter> WavWriter::open(const std::string& path, int sampleRate, int channels, std::string* error)
{
    if (sampleRate <= 0 || channels <= 0 || channels > 8) {
        if (error)
            *error = "unsupported WAV format";
        return nullptr;
    }
    std::FILE* file = std::fopen(path.c_str(), "wb");
    if (!file) {
        if (error)
            *error = std::strerror(errno);
        return nullptr;
    }
    std::unique_ptr<WavWriter> writer(new WavWriter(file, sampleRate, channels));
    if (!writer->writeHeader()) {
        if (error)
            *error = "cannot write the WAV header";
        return nullptr;
    }
    return writer;
}

bool WavWriter::write(const float* samples, std::int64_t frames)
{
    if (!m_file || m_failed)
        return false;
    if (frames <= 0)
        return true;
    // The data chunk's size is 32 bits.
    const std::int64_t maxFrames =
        (std::int64_t(std::numeric_limits<std::uint32_t>::max()) - kHeaderSize) / (kBytesPerSample * m_channels);
    if (m_frames + frames > maxFrames) {
        m_failed = true;
        return false;
    }
    const std::size_t count = std::size_t(frames) * std::size_t(m_channels);
    if (std::fwrite(samples, sizeof(float), count, m_file) != count) {
        m_failed = true;
        return false;
    }
    m_frames += frames;
    return true;
}

bool WavWriter::writeHeader()
{
    const std::uint32_t dataBytes = std::uint32_t(m_frames * m_channels * kBytesPerSample);
    std::array<std::uint8_t, kHeaderSize> header{};
    std::uint8_t* p = header.data();
    putTag(p, "RIFF");
    put32(p, std::uint32_t(kHeaderSize - 8) + dataBytes);
    putTag(p, "WAVE");
    putTag(p, "fmt ");
    put32(p, 18);
    put16(p, kFormatIeeeFloat);
    put16(p, std::uint16_t(m_channels));
    put32(p, std::uint32_t(m_sampleRate));
    put32(p, std::uint32_t(m_sampleRate * m_channels * kBytesPerSample));
    put16(p, std::uint16_t(m_channels * kBytesPerSample));
    put16(p, kBytesPerSample * 8);
    put16(p, 0); // no extension
    putTag(p, "fact"); // required for non-PCM formats: frames per channel
    put32(p, 4);
    put32(p, std::uint32_t(m_frames));
    putTag(p, "data");
    put32(p, dataBytes);

    const long end = std::ftell(m_file);
    if (std::fseek(m_file, 0, SEEK_SET) != 0 || std::fwrite(header.data(), 1, header.size(), m_file) != header.size())
        return false;
    return end <= long(kHeaderSize) || std::fseek(m_file, end, SEEK_SET) == 0;
}

bool WavWriter::flush()
{
    if (!m_file)
        return false;
    if (!writeHeader() || std::fflush(m_file) != 0)
        m_failed = true;
    return !m_failed;
}

bool WavWriter::finish()
{
    if (!m_file)
        return !m_failed;
    flush();
    if (std::fclose(m_file) != 0)
        m_failed = true;
    m_file = nullptr;
    return !m_failed;
}

} // namespace tve
