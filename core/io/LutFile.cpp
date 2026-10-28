#include "io/LutFile.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <sstream>

namespace tve {

namespace {

std::string_view trim(std::string_view s)
{
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
        s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
        s.remove_suffix(1);
    return s;
}

std::vector<std::string_view> words(std::string_view line)
{
    std::vector<std::string_view> out;
    std::size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i])))
            ++i;
        const std::size_t start = i;
        while (i < line.size() && !std::isspace(static_cast<unsigned char>(line[i])))
            ++i;
        if (i > start)
            out.push_back(line.substr(start, i - start));
    }
    return out;
}

bool number(std::string_view word, double& out)
{
    const auto result = std::from_chars(word.data(), word.data() + word.size(), out);
    return result.ec == std::errc() && result.ptr == word.data() + word.size() && std::isfinite(out);
}

// Calls f(line, lineNumber) for every line without its comment, skipping blank ones.
template <typename F>
void forEachLine(std::string_view text, F f)
{
    int number = 0;
    while (!text.empty()) {
        const auto end = text.find('\n');
        std::string_view line = text.substr(0, end);
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        ++number;
        if (const auto hash = line.find('#'); hash != std::string_view::npos)
            line = line.substr(0, hash);
        line = trim(line);
        if (!line.empty())
            f(line, number);
    }
}

void checkSize(int size)
{
    if (size < kMinLutSize || size > kMaxLutSize)
        throw LutError("LUT size must be between " + std::to_string(kMinLutSize) + " and " + std::to_string(kMaxLutSize));
}

} // namespace

std::array<float, 3> Lut3d::at(int r, int g, int b) const
{
    const std::size_t i = (std::size_t(b) * std::size_t(size) + std::size_t(g)) * std::size_t(size) + std::size_t(r);
    return {rgb[i * 3], rgb[i * 3 + 1], rgb[i * 3 + 2]};
}

std::array<float, 3> Lut3d::lookup(std::array<float, 3> in) const
{
    std::array<float, 3> position{};
    std::array<int, 3> lo{};
    std::array<float, 3> t{};
    for (int c = 0; c < 3; ++c) {
        const float range = domainMax[c] - domainMin[c];
        const float u = std::clamp((in[c] - domainMin[c]) / (range > 0 ? range : 1.0f), 0.0f, 1.0f);
        position[c] = u * float(size - 1);
        lo[c] = std::min(int(position[c]), size - 2);
        t[c] = position[c] - float(lo[c]);
    }
    std::array<float, 3> out{0, 0, 0};
    for (int corner = 0; corner < 8; ++corner) {
        const int dr = corner & 1, dg = (corner >> 1) & 1, db = (corner >> 2) & 1;
        const float w = (dr ? t[0] : 1 - t[0]) * (dg ? t[1] : 1 - t[1]) * (db ? t[2] : 1 - t[2]);
        const auto v = at(lo[0] + dr, lo[1] + dg, lo[2] + db);
        for (int c = 0; c < 3; ++c)
            out[c] += w * v[c];
    }
    return out;
}

Lut3d parseCubeLut(std::string_view text)
{
    Lut3d lut;
    std::size_t expected = 0;
    forEachLine(text, [&](std::string_view line, int lineNumber) {
        const auto w = words(line);
        const std::string_view key = w.front();
        if (key == "TITLE") {
            const auto open = line.find('"');
            const auto close = line.rfind('"');
            lut.title = open != close ? std::string(line.substr(open + 1, close - open - 1)) : std::string();
            return;
        }
        if (key == "LUT_1D_SIZE" || key == "LUT_1D_INPUT_RANGE")
            throw LutError("1D LUTs are not supported; use a 3D LUT");
        if (key == "LUT_3D_SIZE") {
            double size = 0;
            if (w.size() != 2 || !number(w[1], size) || size != std::floor(size))
                throw LutError("bad LUT_3D_SIZE on line " + std::to_string(lineNumber));
            lut.size = int(size);
            checkSize(lut.size);
            expected = std::size_t(lut.size) * std::size_t(lut.size) * std::size_t(lut.size) * 3;
            lut.rgb.reserve(expected);
            return;
        }
        if (key == "DOMAIN_MIN" || key == "DOMAIN_MAX" || key == "LUT_3D_INPUT_RANGE") {
            std::array<double, 3> v{};
            const std::size_t count = key == "LUT_3D_INPUT_RANGE" ? 2 : 3;
            if (w.size() != count + 1)
                throw LutError("bad " + std::string(key) + " on line " + std::to_string(lineNumber));
            for (std::size_t i = 0; i < count; ++i) {
                if (!number(w[i + 1], v[i]))
                    throw LutError("bad " + std::string(key) + " on line " + std::to_string(lineNumber));
            }
            if (key == "LUT_3D_INPUT_RANGE") {
                lut.domainMin = {float(v[0]), float(v[0]), float(v[0])};
                lut.domainMax = {float(v[1]), float(v[1]), float(v[1])};
            } else {
                auto& target = key == "DOMAIN_MIN" ? lut.domainMin : lut.domainMax;
                target = {float(v[0]), float(v[1]), float(v[2])};
            }
            return;
        }
        if (std::isalpha(static_cast<unsigned char>(key.front())))
            return; // other keywords (e.g. LUT_IN_VIDEO_RANGE) change nothing we use
        if (lut.size == 0)
            throw LutError("table values before LUT_3D_SIZE on line " + std::to_string(lineNumber));
        if (w.size() != 3)
            throw LutError("expected three numbers on line " + std::to_string(lineNumber));
        if (lut.rgb.size() >= expected)
            throw LutError("more table rows than LUT_3D_SIZE says");
        for (const auto& word : w) {
            double v = 0;
            if (!number(word, v))
                throw LutError("bad number on line " + std::to_string(lineNumber));
            lut.rgb.push_back(float(v));
        }
    });
    if (lut.size == 0)
        throw LutError("no LUT_3D_SIZE: not a 3D .cube LUT");
    if (lut.rgb.size() != expected)
        throw LutError("the table has " + std::to_string(lut.rgb.size() / 3) + " rows, expected "
                       + std::to_string(expected / 3));
    for (int c = 0; c < 3; ++c) {
        if (!(lut.domainMax[c] > lut.domainMin[c]))
            throw LutError("DOMAIN_MAX must be above DOMAIN_MIN");
    }
    return lut;
}

Lut3d parse3dlLut(std::string_view text)
{
    std::vector<std::vector<double>> rows;
    forEachLine(text, [&](std::string_view line, int lineNumber) {
        const auto w = words(line);
        if (std::isalpha(static_cast<unsigned char>(w.front().front())))
            return; // keywords some writers add (e.g. "3DMESH", "Mesh 4 12")
        std::vector<double> values;
        for (const auto& word : w) {
            double v = 0;
            if (!number(word, v) || v < 0)
                throw LutError("bad number on line " + std::to_string(lineNumber));
            values.push_back(v);
        }
        rows.push_back(std::move(values));
    });
    // The input mesh comes first: a row of other than three values (its length is the
    // size), or, for a size-3 table, a row of three that leaves a whole cube of rows after it.
    const auto isCube = [](std::size_t n) {
        const auto root = std::size_t(std::lround(std::cbrt(double(n))));
        return root * root * root == n;
    };
    int size = 0;
    if (!rows.empty() && (rows.front().size() != 3 || (!isCube(rows.size()) && isCube(rows.size() - 1)))) {
        size = int(rows.front().size());
        rows.erase(rows.begin());
    } else {
        size = int(std::lround(std::cbrt(double(rows.size()))));
    }
    checkSize(size);
    const std::size_t count = std::size_t(size) * std::size_t(size) * std::size_t(size);
    if (rows.size() != count)
        throw LutError("the table has " + std::to_string(rows.size()) + " rows, expected " + std::to_string(count));
    double largest = 0;
    for (const auto& row : rows) {
        if (row.size() != 3)
            throw LutError("expected three numbers per table row");
        largest = std::max({largest, row[0], row[1], row[2]});
    }
    const double scale = largest <= 1023 ? 1023 : largest <= 4095 ? 4095 : 65535;
    Lut3d lut;
    lut.size = size;
    lut.rgb.resize(count * 3);
    // Rows run with blue fastest: row (r, g, b) at (r·N + g)·N + b.
    std::size_t row = 0;
    for (int r = 0; r < size; ++r) {
        for (int g = 0; g < size; ++g) {
            for (int b = 0; b < size; ++b, ++row) {
                const std::size_t i = (std::size_t(b) * std::size_t(size) + std::size_t(g)) * std::size_t(size) + std::size_t(r);
                for (int c = 0; c < 3; ++c)
                    lut.rgb[i * 3 + std::size_t(c)] = float(rows[row][std::size_t(c)] / scale);
            }
        }
    }
    return lut;
}

Lut3d loadLut(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        throw LutError("cannot open " + path.filename().string());
    std::ostringstream text;
    text << file.rdbuf();
    std::string extension = path.extension().string();
    std::ranges::transform(extension, extension.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    if (extension == ".cube")
        return parseCubeLut(text.str());
    if (extension == ".3dl")
        return parse3dlLut(text.str());
    throw LutError("unsupported LUT format " + extension + " (use .cube or .3dl)");
}

} // namespace tve
