#pragma once

// A camera RAW photo for tests, written by hand (no camera or download needed): a DNG — a
// TIFF whose first picture is a small RGB preview and whose sub-picture is the raw Bayer
// mosaic (RGGB, 16 bits a sample, white at 4095). The scene is red on the left half and
// blue on the right, the preview a quarter of the size. macOS's RAW support takes it for a
// camera RAW photo ("com.adobe.raw-image") from 256 pixels wide; smaller, it reads only the
// preview, as a TIFF.

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace tve::testing {

inline bool writeTestDng(const std::string& path, int width = 256, int height = 192)
{
    using Bytes = std::vector<std::uint8_t>;
    const auto put16 = [](Bytes& b, std::uint32_t v) { b.push_back(std::uint8_t(v)), b.push_back(std::uint8_t(v >> 8)); };
    const auto put32 = [&](Bytes& b, std::uint32_t v) { put16(b, v & 0xFFFF), put16(b, v >> 16); };

    const int previewW = std::max(1, width / 4), previewH = std::max(1, height / 4);
    Bytes raw, preview;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            const bool left = x < width / 2;
            const std::uint32_t r = left ? 3000 : 300, g = 300, b = left ? 300 : 3000;
            const std::uint32_t mosaic[2][2] = {{r, g}, {g, b}};
            put16(raw, mosaic[y % 2][x % 2]);
        }
    for (int y = 0; y < previewH; ++y)
        for (int x = 0; x < previewW; ++x) {
            const bool left = x < previewW / 2;
            preview.insert(preview.end(), {std::uint8_t(left ? 230 : 20), 20, std::uint8_t(left ? 20 : 230)});
        }

    // An entry: tag, type (1 byte, 2 text, 3 short, 4 long, 5 rational, 10 signed rational),
    // count, and its value's bytes, or a named offset filled in once the layout is known.
    struct Entry {
        std::uint16_t tag, type;
        std::uint32_t count;
        Bytes value;
        std::string offsetOf;
    };
    const auto shorts = [&](std::initializer_list<std::uint32_t> v) { Bytes b; for (auto x : v) put16(b, x); return b; };
    const auto longs = [&](std::initializer_list<std::uint32_t> v) { Bytes b; for (auto x : v) put32(b, x); return b; };
    const auto ratios = [&](std::initializer_list<std::pair<std::uint32_t, std::uint32_t>> v) {
        Bytes b;
        for (auto [n, d] : v)
            put32(b, n), put32(b, d);
        return b;
    };
    const auto text = [](const char* s) { return Bytes(s, s + std::char_traits<char>::length(s) + 1); };
    const Bytes model = text("TVE Test Camera");
    const auto one = std::pair<std::uint32_t, std::uint32_t>{1, 1}, zero = std::pair<std::uint32_t, std::uint32_t>{0, 1};

    std::vector<Entry> first = {
        {254, 4, 1, longs({1}), {}}, {256, 4, 1, longs({std::uint32_t(previewW)}), {}},
        {257, 4, 1, longs({std::uint32_t(previewH)}), {}}, {258, 3, 3, shorts({8, 8, 8}), {}},
        {259, 3, 1, shorts({1}), {}}, {262, 3, 1, shorts({2}), {}}, {271, 2, 4, text("TVE"), {}},
        {272, 2, std::uint32_t(model.size()), model, {}}, {273, 4, 1, {}, "preview"}, {274, 3, 1, shorts({1}), {}},
        {277, 3, 1, shorts({3}), {}}, {278, 4, 1, longs({std::uint32_t(previewH)}), {}},
        {279, 4, 1, longs({std::uint32_t(preview.size())}), {}}, {284, 3, 1, shorts({1}), {}},
        {330, 4, 1, {}, "sub"}, {50706, 1, 4, Bytes{1, 4, 0, 0}, {}}, {50707, 1, 4, Bytes{1, 1, 0, 0}, {}},
        {50708, 2, std::uint32_t(model.size()), model, {}},
        {50721, 10, 9, ratios({one, zero, zero, zero, one, zero, zero, zero, one}), {}},
        {50728, 5, 3, ratios({one, one, one}), {}}, {50778, 3, 1, shorts({21}), {}},
    };
    std::vector<Entry> sub = {
        {254, 4, 1, longs({0}), {}}, {256, 4, 1, longs({std::uint32_t(width)}), {}},
        {257, 4, 1, longs({std::uint32_t(height)}), {}}, {258, 3, 1, shorts({16}), {}}, {259, 3, 1, shorts({1}), {}},
        {262, 3, 1, shorts({32803}), {}}, {273, 4, 1, {}, "raw"}, {277, 3, 1, shorts({1}), {}},
        {278, 4, 1, longs({std::uint32_t(height)}), {}}, {279, 4, 1, longs({std::uint32_t(raw.size())}), {}},
        {284, 3, 1, shorts({1}), {}}, {33421, 3, 2, shorts({2, 2}), {}}, {33422, 1, 4, Bytes{0, 1, 1, 2}, {}},
        {50717, 4, 1, longs({4095}), {}},
    };

    // A directory at `base`: its entries, then the values too long to fit in one.
    struct Packed {
        Bytes bytes;
        std::vector<std::pair<std::size_t, std::string>> fixups;
    };
    const auto pack = [&](std::vector<Entry>& entries, std::uint32_t base) {
        std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return a.tag < b.tag; });
        Packed out;
        Bytes values;
        const std::uint32_t valuesAt = base + 2 + 12 * std::uint32_t(entries.size()) + 4;
        put16(out.bytes, std::uint32_t(entries.size()));
        for (const Entry& e : entries) {
            put16(out.bytes, e.tag), put16(out.bytes, e.type), put32(out.bytes, e.count);
            if (!e.offsetOf.empty()) {
                out.fixups.push_back({out.bytes.size(), e.offsetOf});
                put32(out.bytes, 0);
            } else if (e.value.size() > 4) {
                if (values.size() % 2)
                    values.push_back(0);
                put32(out.bytes, valuesAt + std::uint32_t(values.size()));
                values.insert(values.end(), e.value.begin(), e.value.end());
            } else {
                Bytes v = e.value;
                v.resize(4, 0);
                out.bytes.insert(out.bytes.end(), v.begin(), v.end());
            }
        }
        put32(out.bytes, 0); // no next directory
        out.bytes.insert(out.bytes.end(), values.begin(), values.end());
        if (out.bytes.size() % 2)
            out.bytes.push_back(0);
        return out;
    };
    Packed ifd0 = pack(first, 8);
    const std::uint32_t subAt = 8 + std::uint32_t(ifd0.bytes.size());
    Packed ifd1 = pack(sub, subAt);
    const std::uint32_t previewAt = subAt + std::uint32_t(ifd1.bytes.size());
    std::uint32_t rawAt = previewAt + std::uint32_t(preview.size());
    rawAt += rawAt % 2;
    const auto fill = [&](Packed& p) {
        for (auto& [at, name] : p.fixups) {
            const std::uint32_t v = name == "preview" ? previewAt : name == "sub" ? subAt : rawAt;
            for (int i = 0; i < 4; ++i)
                p.bytes[at + std::size_t(i)] = std::uint8_t(v >> (8 * i));
        }
    };
    fill(ifd0), fill(ifd1);

    Bytes file = {'I', 'I', 42, 0};
    put32(file, 8);
    file.insert(file.end(), ifd0.bytes.begin(), ifd0.bytes.end());
    file.insert(file.end(), ifd1.bytes.begin(), ifd1.bytes.end());
    file.insert(file.end(), preview.begin(), preview.end());
    file.resize(rawAt, 0);
    file.insert(file.end(), raw.begin(), raw.end());
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(file.data()), std::streamsize(file.size()));
    return bool(out);
}

} // namespace tve::testing
