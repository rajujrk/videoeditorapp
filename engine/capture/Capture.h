#pragma once

#include "model/Time.h"

#include <CoreVideo/CoreVideo.h>

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace tve {

// Where recordings come from: the screen (ScreenCaptureKit: a display, a window, or part of
// a display, with the system's sound), a camera (AVFoundation), or a test source that draws
// moving pictures and a tone (for tests: no devices, no permissions).

struct CaptureCallbacks {
    // A picture (NV12, valid during the call) and when it was taken (any clock; increasing).
    std::function<void(CVPixelBufferRef, Ticks)> frame;
    // Interleaved stereo float samples at 48 kHz (the screen's system sound).
    std::function<void(const float*, int)> sound;
};

class CaptureSource {
public:
    virtual ~CaptureSource() = default;
    virtual bool start(CaptureCallbacks callbacks, std::string* error) = 0;
    virtual void stop() = 0;
    // The pictures' size (pixels) and whether there is sound.
    virtual int width() const = 0;
    virtual int height() const = 0;
    virtual bool hasSound() const { return false; }
    // The area captured, in global screen points (origin top left), for the cursor log;
    // nullopt for cameras.
    virtual std::optional<std::array<double, 4>> screenArea() const { return std::nullopt; }
};

// What can be recorded on screen: displays, then windows (title "App — Window").
struct ScreenTarget {
    enum class Kind { Display, Window } kind = Kind::Display;
    std::uint32_t id = 0;
    std::string title;
    double x = 0, y = 0, width = 0, height = 0; // points, global, origin top left
};
std::vector<ScreenTarget> screenTargets(std::string* error = nullptr);

// `region` (points, within a display's own area) records part of a display.
std::unique_ptr<CaptureSource> makeScreenSource(const ScreenTarget& target, std::optional<std::array<double, 4>> region,
                                                bool systemSound, int frameRate = 30);

struct CameraDevice {
    std::string id;
    std::string name;
};
std::vector<CameraDevice> cameras();
std::unique_ptr<CaptureSource> makeCameraSource(const std::string& id, int frameRate = 30);

// Moving bars and a frame counter at `width` × `height`, and (with sound) a 440 Hz tone.
std::unique_ptr<CaptureSource> makeTestSource(int width, int height, int frameRate, bool sound);

// Permissions: the screen (System Settings › Privacy & Security › Screen & System Audio
// Recording) and the camera.
enum class CaptureAccess { Granted, Denied, Undetermined };
CaptureAccess screenAccess();
void requestScreenAccess(); // macOS shows its prompt (or the settings) once
CaptureAccess cameraAccess();
void requestCameraAccess(std::function<void(bool granted)> done); // `done` on another thread

} // namespace tve
