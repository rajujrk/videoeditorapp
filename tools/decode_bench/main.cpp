// Measures raw decode throughput (no display): how many frames per second the engine's
// decoder can deliver for a file.
#include "decode/VideoDecoder.h"

#include <chrono>
#include <cstdio>

int main(int argc, char* argv[])
{
    if (argc < 2) {
        std::fprintf(stderr, "usage: tve_decode_bench <video-file>\n");
        return 2;
    }

    std::string error;
    auto decoder = tve::VideoDecoder::open(argv[1], &error);
    if (!decoder) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return 1;
    }

    const auto start = std::chrono::steady_clock::now();
    long frames = 0;
    while (decoder->nextFrame())
        ++frames;
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

    const auto& info = decoder->info();
    std::printf("%s: %dx%d %s, %ld frames in %.2f s = %.1f fps (%s)\n", argv[1], info.width, info.height,
                info.codecName.c_str(), frames, seconds, static_cast<double>(frames) / seconds,
                info.hardwareAccelerated ? "videotoolbox" : "software");
    return frames > 0 ? 0 : 1;
}
