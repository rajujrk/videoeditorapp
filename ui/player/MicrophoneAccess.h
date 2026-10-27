#pragma once

#include <QObject>

#include <functional>

// The system's permission for this app to use the microphone (macOS asks the person once;
// the answer is kept in System Settings › Privacy & Security › Microphone).
enum class MicrophoneAccess { Granted, Denied, Undetermined };

MicrophoneAccess microphoneAccess();

// Asks for access if it is undetermined, then calls `done` on the main thread with the
// answer (not at all if `context` is gone by then).
void requestMicrophoneAccess(QObject* context, std::function<void(bool granted)> done);
