#include "encode/GifPalette.h"

#include <algorithm>
#include <vector>

namespace tve {

namespace {

constexpr int kBins = 32 * 32 * 32;

int binOf(int r, int g, int b)
{
    return ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3);
}

// A box of histogram bins: the colours it holds and how many pixels have them.
struct Box {
    std::vector<int> bins;
    std::uint64_t pixels = 0;
    int low[3] = {31, 31, 31};
    int high[3] = {0, 0, 0};

    int channel(int bin, int c) const { return (bin >> (10 - 5 * c)) & 31; }
    void measure(const std::vector<std::uint32_t>& counts)
    {
        pixels = 0;
        for (int c = 0; c < 3; ++c) {
            low[c] = 31;
            high[c] = 0;
        }
        for (int bin : bins) {
            pixels += counts[std::size_t(bin)];
            for (int c = 0; c < 3; ++c) {
                low[c] = std::min(low[c], channel(bin, c));
                high[c] = std::max(high[c], channel(bin, c));
            }
        }
    }
    int widest() const
    {
        int best = 0;
        for (int c = 1; c < 3; ++c)
            if (high[c] - low[c] > high[best] - low[best])
                best = c;
        return best;
    }
    int range() const { return high[widest()] - low[widest()]; }
};

// 4 × 4 Bayer matrix, 0–15.
constexpr int kBayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

} // namespace

int quantizeFrame(const std::uint8_t* rgba, int width, int height, int stride, std::uint8_t* indices, int indexStride,
                  GifPalette& palette)
{
    std::vector<std::uint32_t> counts(kBins, 0);
    for (int y = 0; y < height; ++y) {
        const std::uint8_t* row = rgba + std::ptrdiff_t(y) * stride;
        for (int x = 0; x < width; ++x, row += 4)
            ++counts[std::size_t(binOf(row[0], row[1], row[2]))];
    }
    Box all;
    for (int bin = 0; bin < kBins; ++bin)
        if (counts[std::size_t(bin)])
            all.bins.push_back(bin);
    all.measure(counts);

    // Split the box with the most pixels × widest range at the median pixel of that channel.
    std::vector<Box> boxes{std::move(all)};
    while (boxes.size() < palette.size()) {
        auto target = boxes.end();
        double best = 0;
        for (auto it = boxes.begin(); it != boxes.end(); ++it) {
            const double score = double(it->pixels) * it->range();
            if (it->bins.size() > 1 && score > best) {
                best = score;
                target = it;
            }
        }
        if (target == boxes.end())
            break;
        const int c = target->widest();
        std::ranges::sort(target->bins, {}, [&](int bin) { return target->channel(bin, c); });
        std::uint64_t seen = 0;
        std::size_t cut = 1;
        for (; cut < target->bins.size(); ++cut) {
            seen += counts[std::size_t(target->bins[cut - 1])];
            if (seen * 2 >= target->pixels)
                break;
        }
        cut = std::clamp<std::size_t>(cut, 1, target->bins.size() - 1);
        Box upper;
        upper.bins.assign(target->bins.begin() + std::ptrdiff_t(cut), target->bins.end());
        target->bins.resize(cut);
        target->measure(counts);
        upper.measure(counts);
        boxes.push_back(std::move(upper));
    }

    // Each box's colour is the pixel-weighted mean of its bins (at their centres).
    palette.fill(0xFF000000u);
    std::vector<std::array<int, 3>> colours;
    for (const Box& box : boxes) {
        double sum[3] = {0, 0, 0};
        for (int bin : box.bins)
            for (int c = 0; c < 3; ++c)
                sum[c] += double(counts[std::size_t(bin)]) * (box.channel(bin, c) * 8 + 4);
        std::array<int, 3> mean{};
        for (int c = 0; c < 3; ++c)
            mean[c] = box.pixels ? std::clamp(int(sum[c] / double(box.pixels) + 0.5), 0, 255) : 0;
        palette[colours.size()] = 0xFF000000u | (std::uint32_t(mean[0]) << 16) | (std::uint32_t(mean[1]) << 8)
                                | std::uint32_t(mean[2]);
        colours.push_back(mean);
    }

    // Map pixels (dithered) to the nearest colour, each bin worked out once.
    std::vector<std::int16_t> nearest(kBins, -1);
    const auto lookup = [&](int bin) {
        std::int16_t& found = nearest[std::size_t(bin)];
        if (found < 0) {
            const int r = ((bin >> 10) & 31) * 8 + 4;
            const int g = ((bin >> 5) & 31) * 8 + 4;
            const int b = (bin & 31) * 8 + 4;
            int best = 0;
            int bestDistance = 1 << 30;
            for (std::size_t i = 0; i < colours.size(); ++i) {
                const int dr = colours[i][0] - r;
                const int dg = colours[i][1] - g;
                const int db = colours[i][2] - b;
                const int distance = 2 * dr * dr + 4 * dg * dg + db * db;
                if (distance < bestDistance) {
                    bestDistance = distance;
                    best = int(i);
                }
            }
            found = std::int16_t(best);
        }
        return std::uint8_t(found);
    };
    for (int y = 0; y < height; ++y) {
        const std::uint8_t* row = rgba + std::ptrdiff_t(y) * stride;
        std::uint8_t* out = indices + std::ptrdiff_t(y) * indexStride;
        for (int x = 0; x < width; ++x, row += 4) {
            const int offset = (kBayer[y & 3][x & 3] * 2 - 15) / 2; // about ±7, under one bin
            out[x] = lookup(binOf(std::clamp(row[0] + offset, 0, 255), std::clamp(row[1] + offset, 0, 255),
                                  std::clamp(row[2] + offset, 0, 255)));
        }
    }
    return int(colours.size());
}

} // namespace tve
