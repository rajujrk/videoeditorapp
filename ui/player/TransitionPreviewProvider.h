#pragma once

#include <QQuickImageProvider>

// Library previews: image://tvetransition/<transition id>@<progress 0–1> is that transition
// between two sample pictures, rendered offscreen on the GPU (and cached).
class TransitionPreviewProvider : public QQuickImageProvider {
public:
    static constexpr const char* kId = "tvetransition";
    static constexpr int kWidth = 192;
    static constexpr int kHeight = 108;

    TransitionPreviewProvider();
    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;
};
