#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

// Pictures read by Apple's ImageIO: what Qt's image readers leave out, such as camera RAW
// (CR2, CR3, NEF, ARW, RAF, DNG and the others macOS knows) and OpenEXR, and the
// frames of an image sequence of any of those or PNG, JPEG, TIFF, HEIC, WebP…
namespace tve::imagefile {

struct Info {
    int width = 0;  // the right way up (its orientation applied)
    int height = 0;
    bool alpha = false;  // has transparency
    int count = 0;       // pictures in the file (an animated one has several)
    bool raw = false;    // a camera RAW photo
    std::string type;    // its uniform type identifier ("com.canon.cr3-raw-image")
};

// What the file is, if ImageIO reads it.
std::optional<Info> probe(const std::string& path);

enum class Layout { Bgra, Rgba };

// Draws the file's (first) picture the right way up, scaled to exactly width × height,
// into `dest` as 8-bit sRGB with straight (not premultiplied) alpha.
bool decodeInto(const std::string& path, int width, int height, Layout layout, std::uint8_t* dest,
                std::size_t bytesPerRow);

} // namespace tve::imagefile
