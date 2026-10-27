#pragma once

#include "gpu/EffectLibrary.h"

#include <QVariantMap>

// An effect's or transition's manifest as QML sees it: {id, title, categories, params}, each
// param {id, title, type, min, max, default, values}.
QVariantMap describeEffect(const tve::LoadedEffect& effect);
