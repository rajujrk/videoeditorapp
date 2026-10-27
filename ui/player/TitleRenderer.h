#pragma once

#include "model/Project.h"

#include <QImage>
#include <QQuickImageProvider>
#include <QSize>
#include <QStringList>
#include <QVariantMap>

// Draws a title's text as it appears on a canvas of `canvas` pixels: wrapped at 90 % of
// the width, aligned, centred vertically, with optional outline and soft shadow. The image
// is canvas-sized with straight (not premultiplied) alpha, transparent around the text.
QImage renderTitle(const tve::ClipText& text, QSize canvas);

// The font families offered for titles: a curated list, keeping those installed.
QStringList titleFontFamilies();

// The family used when a title names none (or one that is not installed).
QString defaultTitleFamily();

// A title's text as QML sees it: {text, fontFamily, size, bold, italic, color [r,g,b,a],
// alignment ("left", "center", "right"), lineSpacing, letterSpacing, outlineColor,
// outlineWidth, shadow}.
QVariantMap textMap(const tve::ClipText& text);
// `base` with the keys present in `changes` applied (unknown keys are ignored).
tve::ClipText textFrom(const QVariantMap& changes, tve::ClipText base = {});

// Library previews: image://tvetitle/<url-encoded JSON {text fields…, y}> is that title on a
// small canvas, moved down by y (a fraction of the height, like the clip's position).
class TitlePreviewProvider : public QQuickImageProvider {
public:
    static constexpr const char* kId = "tvetitle";
    static constexpr int kWidth = 320;
    static constexpr int kHeight = 180;

    TitlePreviewProvider();
    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;
};
