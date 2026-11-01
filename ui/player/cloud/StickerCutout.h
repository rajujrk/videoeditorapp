#pragma once

#include <QString>

// Makes a generated picture a sticker: kept as it is when it already has a transparent
// background, else its subject is lifted out with Apple Vision (on this Mac) and the rest
// made transparent, in place. False (with `error`) when there is no subject to lift.
bool cutOutSticker(const QString& path, QString* error = nullptr);
