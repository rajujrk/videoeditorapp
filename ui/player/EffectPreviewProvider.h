#pragma once

#include <QQuickImageProvider>

// Library previews: image://tveeffect/<effect id>@<seconds> is that effect at its defaults on
// a sample scene, at that moment (for animated effects), rendered offscreen on the GPU and
// cached. Canvas effects (shadows, outlines, fills) show the scene inset on a neutral canvas.
class EffectPreviewProvider : public QQuickImageProvider {
public:
    static constexpr const char* kId = "tveeffect";
    static constexpr int kWidth = 192;
    static constexpr int kHeight = 108;

    EffectPreviewProvider();
    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;
};
